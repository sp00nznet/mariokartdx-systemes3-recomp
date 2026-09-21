/*
 * synthpad.c - a DirectInput controller this runtime makes up.
 *
 * The game asks DirectInput for game controllers once, at startup. With no
 * pad attached it finds none, and then there is no input at all - no buttons,
 * no steering, no device record, and nothing to measure. That is a poor place
 * to be stuck: a sleeping Bluetooth pad voided six test runs in one session
 * and the steering question went unanswered for want of any device.
 *
 * So make one. This runtime already stands in for a USB I/O board and for a
 * PN53x card reader; a game controller is the same kind of lie, told to the
 * same purpose.
 *
 * How it attaches:
 *
 *   - 0x00740090 enumerates DI8DEVCLASS_GAMECTRL. At its entry the game's
 *     IDirectInput8 is already in 0x00952CB4, so its vtable can be patched:
 *     slot 4 EnumDevices and slot 3 CreateDevice become ours.
 *   - our EnumDevices runs the real one first, so a real pad still wins, and
 *     only if nothing was found does it call the game's own callback with a
 *     device instance we filled in.
 *   - the callback is 0x0073FF60, which es3_plant_callback() has already made
 *     callable from real code - without that plant none of this would work.
 *   - the game then asks CreateDevice for that instance's GUID, and gets an
 *     IDirectInputDevice8W of ours: a vtable of stdcall stubs, most of which
 *     only have to return DI_OK.
 *
 * What it reports: a DIJOYSTATE2 driven from the keyboard, and the product
 * name the game insists on before it will treat a device as its wheel.
 *
 * ES3_SYNTHETIC_PAD=1 to switch on. Off by default - a machine with a real
 * controller should use it.
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "es3_rt.h"

#define DI_OK           0
#define DI_NOEFFECT     1
#define E_NOTIMPL_HR    ((long)0x80004001L)
#define DIERR_UNSUP     ((long)0x80004001L)

/* DIJOYSTATE2 is 0x110 bytes: lX lY lZ lRx lRy lRz, two sliders, four POVs,
 * 128 buttons, then the velocity/accel/force blocks the game does not read. */
#define DIJOYSTATE2_SIZE 0x110

typedef struct { void **vtbl; long refs; } SynthDevice;

static SynthDevice g_dev;
static void *g_dev_vtbl[32];
static int   g_on = -1;

/* The GUID we answer to. Arbitrary, and deliberately unlike a real one so a
 * device that is not ours can never be confused for it. */
static const GUID k_synth_guid =
    { 0xE53E5300, 0x1234, 0x5678, { 'E','S','3','P','A','D','0','1' } };

static int synth_on(void)
{
    if (g_on < 0) g_on = getenv("ES3_SYNTHETIC_PAD") != NULL;
    return g_on;
}

/* ------------------------------------------------------------ the state - */

/*
 * The keyboard, as a wheel and pedals.
 *
 * Axis values are whatever range the game last asked for through
 * SetProperty(DIPROP_RANGE) - it asks for +/- 1e9, because that is what its
 * own arithmetic divides by - so the range is remembered rather than assumed.
 */
static long g_axis_min = -1000000000L, g_axis_max = 1000000000L;

static void synth_fill_state(unsigned char *out)
{
    long centre = (g_axis_min + g_axis_max) / 2;
    long span   = (g_axis_max - g_axis_min) / 2;
    long x = centre, y = centre, z = centre;
    unsigned i;

    memset(out, 0, DIJOYSTATE2_SIZE);

    #define DOWN(k) ((GetAsyncKeyState(k) & 0x8000) != 0)
    if (DOWN(VK_LEFT))  x = centre - span;
    if (DOWN(VK_RIGHT)) x = centre + span;
    if (DOWN(VK_UP))    z = g_axis_min;          /* accelerator, pressed */
    if (DOWN(VK_DOWN))  y = g_axis_min;          /* brake */
    #undef DOWN

    /*
     * ES3_PAD_SWEEP: turn the wheel from side to side on a timetable.
     *
     * A keyboard needs someone at it, and the point of the synthetic pad is
     * that nobody has to be. A sweep drives the axis through its whole range
     * every few seconds, so the game's own wheel value can be watched
     * following it - which is the difference between believing the steering
     * path works and having seen it.
     */
    {
        static int sweep = -1;
        if (sweep < 0) sweep = getenv("ES3_PAD_SWEEP") != NULL;
        if (sweep) {
            /* A triangle wave, four seconds a side, so the log shows a clean
             * ramp rather than a sine's crowded ends. */
            unsigned long t = GetTickCount() % 8000u;
            long f = (long)(t < 4000u ? t : 8000u - t);   /* 0..4000 */
            x = g_axis_min + (long)((double)(g_axis_max - g_axis_min) *
                                    (double)f / 4000.0);
        }
    }

    /* lX lY lZ lRx lRy lRz at 0x00..0x14 */
    ((long *)out)[0] = x;
    ((long *)out)[1] = y;
    ((long *)out)[2] = z;
    ((long *)out)[3] = centre;
    ((long *)out)[4] = centre;
    ((long *)out)[5] = centre;
    /* rgdwPOV at 0x20: centred is all bits set */
    for (i = 0; i < 4; i++) ((unsigned long *)(out + 0x20))[i] = 0xFFFFFFFFu;

    /* rgbButtons at 0x30, one byte each, 0x80 when pressed */
    #define BTN(n, k) if ((GetAsyncKeyState(k) & 0x8000) != 0) out[0x30 + (n)] = 0x80
    BTN(0, VK_LCONTROL);      /* item */
    BTN(1, 'Z');              /* push 2 */
    BTN(2, VK_RETURN);        /* start */
    BTN(3, 'S');              /* service */
    #undef BTN
}

/* ------------------------------------------------------------- the vtable - */

static long __stdcall s_QueryInterface(void *self, const GUID *iid, void **out)
{ (void)iid; if (out) *out = self; return DI_OK; }
static unsigned long __stdcall s_AddRef(void *self)
{ (void)self; return (unsigned long)++g_dev.refs; }
static unsigned long __stdcall s_Release(void *self)
{ (void)self; return (unsigned long)(g_dev.refs > 0 ? --g_dev.refs : 0); }

/* DIDEVCAPS: dwSize, dwFlags, dwDevType, dwAxes, dwButtons, dwPOVs, ... */
static long __stdcall s_GetCapabilities(void *self, void *caps)
{
    unsigned long *p = (unsigned long *)caps;
    (void)self;
    if (!p) return DIERR_UNSUP;
    p[0] = 0x2C;                       /* dwSize */
    p[1] = 0x00000005;                 /* ATTACHED | POLLEDDEVICE */
    p[2] = 0x00010015;                 /* gamepad */
    p[3] = 5;                          /* axes */
    p[4] = 12;                         /* buttons */
    p[5] = 1;                          /* POVs */
    return DI_OK;
}

/*
 * EnumObjects, which the game uses to count axes. Its callback is
 * 0x0073FF80 - planted, so callable - and it wants a DIDEVICEOBJECTINSTANCEW
 * per axis with dwOfs and dwType set the way a real pad reports them:
 * measured from a real controller, offsets 0/4/8/12/16 and types 0x...02.
 */
static long __stdcall s_EnumObjects(void *self, void *cb, void *ref,
                                    unsigned long flags)
{
    typedef int (__stdcall *EnumCb)(const void *, void *);
    EnumCb fn = (EnumCb)cb;
    unsigned char inst[0x1A4];
    unsigned i;
    static const unsigned long type[5] =
        { 0x00000102u, 0x00000002u, 0x00000402u, 0x00000302u, 0x00000202u };

    (void)self; (void)flags;
    if (!fn) return DI_OK;
    for (i = 0; i < 5; i++) {
        memset(inst, 0, sizeof inst);
        *(unsigned long *)(inst + 0x00) = sizeof inst;   /* dwSize */
        *(unsigned long *)(inst + 0x14) = i * 4u;        /* dwOfs */
        *(unsigned long *)(inst + 0x18) = type[i];       /* dwType */
        if (!fn(inst, ref)) break;                       /* DIENUM_STOP */
    }
    return DI_OK;
}

static long __stdcall s_GetProperty(void *self, const GUID *p, void *ph)
{ (void)self; (void)p; (void)ph; return DI_OK; }

/* SetProperty, where the game asks for the range its maths needs. */
static long __stdcall s_SetProperty(void *self, const GUID *prop,
                                    const void *ph)
{
    (void)self;
    if ((uintptr_t)prop == 4u && ph) {          /* DIPROP_RANGE */
        const long *r = (const long *)((const unsigned char *)ph + 16);
        g_axis_min = r[0];
        g_axis_max = r[1];
    }
    return DI_OK;
}

static long __stdcall s_Acquire(void *self)   { (void)self; return DI_OK; }
static long __stdcall s_Unacquire(void *self) { (void)self; return DI_OK; }

static long __stdcall s_GetDeviceState(void *self, unsigned long n, void *out)
{
    (void)self;
    if (!out) return DIERR_UNSUP;
    if (n < DIJOYSTATE2_SIZE) { memset(out, 0, n); return DI_OK; }
    synth_fill_state((unsigned char *)out);
    return DI_OK;
}

static long __stdcall s_GetDeviceData(void *self, unsigned long a, void *b,
                                      unsigned long *c, unsigned long d)
{ (void)self; (void)a; (void)b; (void)d; if (c) *c = 0; return DI_OK; }
static long __stdcall s_SetDataFormat(void *self, const void *f)
{ (void)self; (void)f; return DI_OK; }
static long __stdcall s_SetEventNotification(void *self, void *h)
{ (void)self; (void)h; return DI_OK; }
static long __stdcall s_SetCooperativeLevel(void *self, void *w,
                                            unsigned long f)
{ (void)self; (void)w; (void)f; return DI_OK; }
static long __stdcall s_GetObjectInfo(void *self, void *i, unsigned long o,
                                      unsigned long h)
{ (void)self; (void)i; (void)o; (void)h; return DI_OK; }

/* GetDeviceInfo: the same instance the enumeration handed over, because the
 * game may ask again and must get the same name both times. */
void es3_synthpad_instance(void *out);
static long __stdcall s_GetDeviceInfo(void *self, void *inst)
{ (void)self; if (inst) es3_synthpad_instance(inst); return DI_OK; }

static long __stdcall s_RunControlPanel(void *self, void *w, unsigned long f)
{ (void)self; (void)w; (void)f; return DI_OK; }
static long __stdcall s_Initialize(void *self, void *h, unsigned long v,
                                   const GUID *g)
{ (void)self; (void)h; (void)v; (void)g; return DI_OK; }
static long __stdcall s_notimpl(void *self)
{ (void)self; return E_NOTIMPL_HR; }
static long __stdcall s_Poll(void *self) { (void)self; return DI_OK; }

static void synth_build_vtable(void)
{
    unsigned i;
    for (i = 0; i < 32; i++) g_dev_vtbl[i] = (void *)s_notimpl;
    g_dev_vtbl[0]  = (void *)s_QueryInterface;
    g_dev_vtbl[1]  = (void *)s_AddRef;
    g_dev_vtbl[2]  = (void *)s_Release;
    g_dev_vtbl[3]  = (void *)s_GetCapabilities;
    g_dev_vtbl[4]  = (void *)s_EnumObjects;
    g_dev_vtbl[5]  = (void *)s_GetProperty;
    g_dev_vtbl[6]  = (void *)s_SetProperty;
    g_dev_vtbl[7]  = (void *)s_Acquire;
    g_dev_vtbl[8]  = (void *)s_Unacquire;
    g_dev_vtbl[9]  = (void *)s_GetDeviceState;
    g_dev_vtbl[10] = (void *)s_GetDeviceData;
    g_dev_vtbl[11] = (void *)s_SetDataFormat;
    g_dev_vtbl[12] = (void *)s_SetEventNotification;
    g_dev_vtbl[13] = (void *)s_SetCooperativeLevel;
    g_dev_vtbl[14] = (void *)s_GetObjectInfo;
    g_dev_vtbl[15] = (void *)s_GetDeviceInfo;
    g_dev_vtbl[16] = (void *)s_RunControlPanel;
    g_dev_vtbl[17] = (void *)s_Initialize;
    g_dev_vtbl[25] = (void *)s_Poll;
    g_dev.vtbl = g_dev_vtbl;
    g_dev.refs = 1;
}

/* ------------------------------------------------------- the instance -- */

/*
 * DIDEVICEINSTANCEW, 0x44C bytes: dwSize, guidInstance, guidProduct,
 * dwDevType, then tszInstanceName at 0x28 and tszProductName at 0x230 - the
 * offset the game reads its wheel's name from, and copies wholesale into its
 * device record with a rep movsd at 0x0074133E.
 */
void es3_synthpad_instance(void *out)
{
    static const wchar_t name[] =
        L"Immersion TouchSense Steering Wheel (USB HID)";
    unsigned char *p = (unsigned char *)out;

    memset(p, 0, 0x44C);
    *(unsigned long *)(p + 0x00) = 0x44C;
    memcpy(p + 0x04, &k_synth_guid, sizeof k_synth_guid);
    memcpy(p + 0x14, &k_synth_guid, sizeof k_synth_guid);
    *(unsigned long *)(p + 0x24) = 0x00010015;      /* gamepad */
    memcpy(p + 0x028, name, sizeof name);
    memcpy(p + 0x230, name, sizeof name);
}

/* ------------------------------------------------------ the attachment -- */

typedef long (__stdcall *EnumDevicesFn)(void *, unsigned long, void *, void *,
                                        unsigned long);
typedef long (__stdcall *CreateDeviceFn)(void *, const GUID *, void **, void *);

static EnumDevicesFn  g_real_enum;
static CreateDeviceFn g_real_create;

static long __stdcall s_EnumDevices(void *self, unsigned long devclass,
                                    void *cb, void *ref, unsigned long flags)
{
    typedef int (__stdcall *DevCb)(const void *, void *);
    long hr = g_real_enum ? g_real_enum(self, devclass, cb, ref, flags) : DI_OK;
    static int announced;

    /*
     * A real controller wins. This only speaks when the real enumeration
     * found nothing, which is the case that otherwise leaves the game with
     * no input at all and nothing to measure.
     */
    if (cb && !es3_synthpad_found_real()) {
        unsigned char inst[0x44C];
        es3_synthpad_instance(inst);
        if (!announced) {
            announced = 1;
            fprintf(stderr, "[pad] no real controller, so this runtime is "
                            "offering the game one: arrow keys steer and "
                            "pedal, Ctrl is the item button, Enter is start "
                            "(ES3_SYNTHETIC_PAD)\n");
            fflush(stderr);
        }
        ((DevCb)cb)(inst, ref);
    }
    return hr;
}

static long __stdcall s_CreateDevice(void *self, const GUID *guid, void **out,
                                     void *outer)
{
    if (guid && memcmp(guid, &k_synth_guid, sizeof k_synth_guid) == 0) {
        synth_build_vtable();
        if (out) *out = &g_dev;
        return DI_OK;
    }
    return g_real_create ? g_real_create(self, guid, out, outer)
                         : DIERR_UNSUP;
}

/* Did the real enumeration produce anything? The game counts devices at
 * manager+8, and the manager hangs off the singleton at +0x0C. */
int es3_synthpad_found_real(void)
{
    uint32_t sing = rd32(0x00959B54u);
    uint32_t mgr;
    if (!sing) return 0;
    mgr = rd32(sing + 0x0Cu);
    if (!mgr) return 0;
    return rd32(mgr + 8u) != 0u;
}

/*
 * Patch the game's IDirectInput8 so the two methods that matter are ours.
 * Called at the entry of 0x00740090, by which time 0x00952CB4 holds the
 * object the game got from DirectInput8Create.
 */
void es3_synthpad_attach(void)
{
    static int done;
    uint32_t obj;
    void **vt;
    DWORD old;

    if (!synth_on() || done) return;

    /* Say why, rather than returning in silence: the first attempt printed
     * nothing at all and there was no way to tell whether the hook had run,
     * the object was missing, or the vtable was. */
    obj = rd32(0x00952CB4u);
    if (!obj) {
        static int told;
        if (!told) {
            told = 1;
            fprintf(stderr, "[pad] the game has no IDirectInput8 in 0x952CB4 "
                            "yet; nothing to patch\n");
            fflush(stderr);
        }
        return;
    }
    vt = *(void ***)(uintptr_t)obj;
    if (!vt) {
        fprintf(stderr, "[pad] IDirectInput8 at %08X has no vtable\n", obj);
        fflush(stderr);
        return;
    }
    done = 1;

    if (!VirtualProtect(vt, 8 * sizeof(void *), PAGE_READWRITE, &old)) return;
    g_real_create = (CreateDeviceFn)vt[3];
    g_real_enum   = (EnumDevicesFn)vt[4];
    vt[3] = (void *)s_CreateDevice;
    vt[4] = (void *)s_EnumDevices;
    VirtualProtect(vt, 8 * sizeof(void *), old, &old);

    fprintf(stderr, "[pad] watching the game's DirectInput for an empty "
                    "enumeration\n");
    fflush(stderr);
}
