/*
 * host.c - run recompiled Mario Kart Arcade GP DX.
 *
 * There is very little to do here, and that is the point. The runtime maps the
 * executable where it was linked, points every IAT slot at the runtime, and
 * gives the guest a stack; the lifted code is the game. This hands control to
 * the PE entry point and gets out of the way.
 *
 * Everything the game reaches for that is not its own code arrives as
 * hle_call(). Most of it is Windows and gets forwarded to the host's own DLLs.
 * What is left is the cabinet - the JVS I/O the wheel and coins arrive on, the
 * card reader, the camera - and each of those aborts naming itself, so the way
 * to work on this port is to run it and read what it asks for.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "es3_rt.h"

#ifdef _WIN32
/*
 * Who ended the run.
 *
 * The game stopped with exit code 0 after twenty-five seconds, with its main
 * thread blocked in WaitForSingleObject and not one of the traced exit paths
 * touched - no `exit`, no `ExitThread`, no `TerminateProcess`. Something
 * called ExitProcess, and an import handler cannot see it because the guest
 * does not import it: it is reached from inside a forwarded library, or from
 * code this runtime never routed.
 *
 * A TLS callback does see it. Windows runs DLL_PROCESS_DETACH on the thread
 * that called ExitProcess, before the process goes, so this names that thread -
 * and the dispatch trail says what that thread was doing.
 */
static void NTAPI on_detach(void *h, DWORD reason, void *reserved)
{
    (void)h; (void)reserved;
    if (reason != DLL_PROCESS_DETACH) return;
    fprintf(stderr, "\n[host] the process is exiting, on thread %lu\n",
            GetCurrentThreadId());
    es3_report_state("who ended it");
    fflush(stderr);
}

#pragma section(".CRT$XLB", long, read)
__declspec(allocate(".CRT$XLB")) PIMAGE_TLS_CALLBACK es3_tls_cb = on_detach;
#pragma comment(linker, "/INCLUDE:__tls_used")
#endif


/*
 * The cabinet's I/O board, answered where the game asks for it.
 *
 * 0x007A8590 is "how many Namco I/O boards are on the USB bus". It builds the
 * USB host controller interface GUID, enumerates every controller, walks the
 * device tree under each one and counts the devices that match. On a cabinet
 * that is one. On a desktop it is zero, and zero is the answer that stops the
 * game dead:
 *
 *     005C1EEF  call 0x7a8590
 *     005C1EFB  cmp eax, 1
 *     005C1EFE  je   0x5c1f38      ; one board: get on with it
 *     005C1F00  jle  0x5c1f1b
 *     005C1F1F  push 0x51          ; none: mode 0x51
 *     005C1F21  call 0x5c2c50
 *
 * Mode 0x51 is one of the forty-one entries in the table at 0x00871A10 whose
 * first word is 1, and 0x005C38B0 returns true when any of the five slots at
 * [[0x959B64]]+0x3C holds such a mode. The frame loop tests exactly that:
 *
 *     006AB9D2  call 0x5c38b0
 *     006AB9D9  je   0x6aba86      ; true: skip the task tick entirely
 *     006AB9FE  call 0x746b90      ; the task tick, never reached
 *
 * So with no board the game runs its frame, ticks nothing, builds no scene and
 * presents an empty back buffer - about twenty times a second, for ever. The
 * slot array reads 00000051 00000066 00000066 00000066 00000066: slot zero in
 * mode 0x51, the rest empty.
 *
 * Emulating the USB tree underneath this would mean faking SetupAPI, the hub
 * IOCTLs and a device descriptor, to answer a question whose honest answer on
 * a machine standing in for a cabinet is "one". So answer it here. cdecl: the
 * caller does its own `add esp, 4`, so only the return address is consumed.
 *
 * ES3_NO_BOARD leaves the real count in place, which is how to see what the
 * game does with no I/O at all.
 */
static int mk_io_board_count(CPU *c)
{
    static int off = -1, said, said_obj;
    if (off < 0) off = getenv("ES3_NO_BOARD") != NULL;
    if (off) return 0;
    if (!said) {
        said = 1;
        fprintf(stderr, "[board] the game asked how many I/O boards are on the "
                        "USB bus; saying one.\n");
    }
    c->eax = 1;
    c->esp += 4;                      /* the return address, as `ret` would */

    /*
     * And the cabinet's identity, which is the very next thing it asks.
     *
     * Answering the board count moves the slot from mode 0x51 to 0x52 - the
     * next mode that suppresses the task tick - because 0x005C1F8D checks the
     * twelve-character cabinet ID immediately afterwards:
     *
     *     005C1F8D  lea  ebx, [edi + 0x498]   ; the ID
     *     005C1FAF  cmp  eax, 0xc             ; twelve characters
     *     005C1FB2  jne  0x5c2206             ; no: mode 0x52
     *     005C1FF3  call wcsspn(ID, "0123456789")
     *     005C1FFC  cmp  eax, 0xc             ; all twelve of them digits
     *     005C1FFF  jne  0x5c2206
     *
     * On a cabinet that is the serial the security keychip holds. Here the
     * field is twelve nuls, which is why the length check fails. It is part of
     * the same object the board check hands its answer to, reachable from the
     * singleton at 0x00959B64, so write it in the same breath - a machine
     * standing in for a cabinet has a serial number like any other.
     *
     * Only when it is empty: a real one from anywhere else wins.
     */
    {
        /* Not twelve arbitrary digits: the game takes the serial apart and
         * checks the pieces (0x005C2041 onwards).
         *
         *   [0..3] _wtoi  == 0xA96 (2710)     the title
         *   [4]                               read, checked later
         *   [5]    _wtoi  <= 3, or 4, or 9    the variant
         *   [6..7] _wtoi  == 2                the revision
         *   [8..11]                           the unit
         *
         * so "2710" "0" "0" "02" "0001". A cabinet's would differ only in the
         * last four. */
        static const wchar_t serial[] = L"271000020001";
        uint32_t via_global = 0, obj = rd32(0x00959B64u);
        if (obj) via_global = rd32(obj);

        /* edi is the object the caller is working on, and it is the one the
         * check reads two dozen instructions later. The singleton should name
         * the same object; say so if it ever does not, because writing the
         * serial into the wrong one looks exactly like writing it into the
         * right one and having it ignored. */
        if (!said_obj) {
            said_obj = 1;
            fprintf(stderr, "[board] object from edi %08X, from the singleton "
                            "%08X%s\n", c->edi, via_global,
                    c->edi == via_global ? "" : "  <- they differ");
        }
        /* Both copies, because the game compares them.
         *
         * +0x498 is the ID as read from the cabinet this run; +0x54 is the one
         * it remembers. 0x005C2284 walks the two and sets mode 0x52 when they
         * differ - a board that has been swapped - and the copy that syncs
         * them (0x005C2214) only runs on the failure path we are trying not to
         * take. Writing one and not the other trades "no ID" for "the ID
         * changed", which is the same black screen. */
        if (c->edi && rd16(c->edi + 0x498u) == 0) {
            memcpy((void *)(uintptr_t)(c->edi + 0x498u), serial, sizeof serial);
            memcpy((void *)(uintptr_t)(c->edi + 0x54u), serial, sizeof serial);
            fprintf(stderr, "[board] and its cabinet ID was blank; "
                            "giving it %ls.\n", serial);
        }

    }
    return 1;
}

/*
 * "Is the network a problem?" - answered no, once, where it is asked.
 *
 * 0x00679470 is the whole question. It reads
 *
 *     [0x0095A850]+0x94   the All.Net client's last error   (2 = DNS failed)
 *     [0x0095A894]        the All.Net client object itself
 *     [0x0095A894]+0xDA4  that object's "not ready" flag
 *
 * and returns 1 if any of them says trouble - including, first, the object
 * simply being null. Its caller at 0x005BF4D0 then either raises E05-55 or,
 * either way, puts the boot task into state 3, which is the screen that says
 * `<OFFLINE OPERATION>` and `PLEASE WAIT` and never leaves.
 *
 * Every one of those three inputs is decided in the first seconds of the
 * boot, and whether the object exists at all varies from run to run - the
 * same run twice gives the panel once and the operator screen once. Holding
 * the fields down from outside at ten hertz cannot win a race that is settled
 * once, which is why several careful pokes did nothing.
 *
 * So answer the question instead of arguing with its inputs. A cabinet whose
 * network is working answers no, and this one's is: the link finds itself,
 * the resolver answers, and allnet.c is listening on the loopback.
 *
 * ES3_NO_NET_OK returns 0 from here, which lets the original run and is how
 * to get the panel back.
 */
/*
 * Every error the cabinet files, as it files it.
 *
 * 0x005C37E0 is AddError(code): the code arrives on the stack, the object is
 * in edi, and the five slots it appends to are at [edi+0x3C]. The panel turns
 * a code into a string through the table at 0x00932080, so the code is the
 * name - 29 is ERROR DNS TIMEOUT, 28 is ERROR TIP HOST NOTFOUND, 56 is E05-55.
 *
 * A pre-hook rather than a watchpoint: ES3_WATCH_MEM has to resolve a heap
 * chain before it can arm, and the errors that decide the boot are filed in
 * the first second, before there is anything to point it at. Returns 0, so
 * the game's own AddError still runs.
 */
static int mk_trace_error(CPU *c)
{
    fprintf(stderr, "[err] AddError(%u) from %08X, object %08X\n",
            A32(0), rd32(c->esp), c->edi);
    return 0;
}

/* And the wrapper above it, which is where the interesting caller is: every
 * AddError arrives from inside 0x005C2C50, so its own return address is the
 * only one that names the site that decided. */
static int mk_trace_raise(CPU *c)
{
    fprintf(stderr, "[err] raise(%u) from %08X\n", A32(0), rd32(c->esp));
    return 0;
}

/*
 * NAMCAM, the cabinet's camera - E08-01, and the reason attract mode was not
 * being drawn at all.
 *
 * The boot's camera task waits for the device and gives up on a count:
 *
 *     005BEB93  cmp dword [edi+0x70], 0x258   ; six hundred frames
 *     005BEB9A  jbe 0x5bebb6                  ; not yet; wait
 *     005BEB9C  cmp dword [edi+0x48], 1       ; still initialising
 *     005BEBA8  push 0x46                     ; then E08-01
 *
 * and E08-01 is not just a line on a panel. 0x005C38B0 returns true when any
 * of the five error slots holds a mode whose entry in the twelve-byte table at
 * 0x00871A10 begins with 1, and the frame loop skips the whole task tick when
 * it does. Mode 70 is such an entry - the same as mode 56, and the same as the
 * 0x51 the missing I/O board used to produce. So the camera failing is the
 * game building no scene, for ever.
 *
 * What it waits on is `[[0x00959B4C]+4]`, and 0x0073ECF0 is what sets it: it
 * asks DirectShow for the video capture category, walks the monikers, checks
 * the first one and writes 1 if it is satisfied. It is a probe and nothing
 * else - every interface it opens is released before it returns, and the flag
 * is the only thing that outlives it. So answering it is answering the same
 * question the I/O board count answers: is the cabinet's hardware here.
 *
 * The object arrives in eax, not ecx, and the function takes no arguments.
 *
 * ES3_NO_CAMERA leaves the real enumeration in place.
 */
/*
 * And the honest half of the same answer: the camera check's OFF path.
 *
 * 0x005BEA80 is the camera task's update, and it has three ways out. With
 * [this+0xAE0] non-zero it polls for the device and, six hundred frames later,
 * raises E08-01. With [this+0xAE0] zero it takes 0x005BEAF0 instead - sets the
 * state to 2, sets [this+0x4C], and the checklist line renders the string at
 * 0x0088AC20, which is "OFF". No error, no wait, and no slot holding a mode
 * that suppresses the task tick.
 *
 * That is what [this+0xAE0] is for: it is the cabinet's camera-fitted setting,
 * and a cabinet without one says so. Saying it here is closer to the truth
 * than mk_camera_present's "there is a device" - which is kept because it is
 * what the probe would answer on a machine that did have a webcam, and
 * ES3_CAMERA_ON selects it.
 *
 * The object arrives in ecx (the function's first act is `mov edi, ecx`).
 */
static int mk_camera_off(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_CAMERA_ON") != NULL;
    if (off || !c->ecx) return 0;
    if (rd32(c->ecx + 0x0AE0u) != 0) {
        wr32(c->ecx + 0x0AE0u, 0);
        if (!said) {
            said = 1;
            fprintf(stderr, "[cam] no NAMCAM on this machine; telling the boot "
                            "the camera is not fitted, which is the answer it "
                            "has a checklist line for (ES3_CAMERA_ON to make "
                            "it look for one).\n");
        }
    }
    return 0;                         /* the game's own update still runs */
}

static int mk_camera_present(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_NO_CAMERA") != NULL;
    if (off || !c->eax) return 0;
    if (!said) {
        said = 1;
        fprintf(stderr, "[cam] the game looked for the NAMCAM on the video "
                        "capture bus; saying it is there (ES3_NO_CAMERA to let "
                        "it look for itself).\n");
    }
    wr8(c->eax + 4u, 1);              /* the flag the boot task polls */
    c->esp += 4;                      /* the return address, as `ret` would */
    return 1;
}

/*
 * The cabinet's security dongle, which a desktop has no slot for.
 *
 * The last gate in the boot is at 0x005BF516, and it is two conditions:
 *
 *     005BF516  cmp dword [ecx+0x90], 0x67       ; All.Net said OK
 *     005BF52A  cmp byte [[0x959B5C]+0xcdc], 0   ; and the cabinet is
 *     005BF530  jne 0x5bf5c5                     ; authenticated: carry on
 *     005BF53C  push 0x38                        ; otherwise E05-55
 *
 * The first passes now that allnet.c answers a PowerOn the client accepts.
 * The second is the dongle. 0x005C23C0 opens `F:/dongle.bin` with fopen("rb"),
 * and if it reads eight bytes it copies them over [this+0xCE0]; the
 * constructor then copies that byte to [this+0xCDC], which is what the gate
 * reads. There is no F: drive here, so the read never happens - and the game's
 * own answer to that is at 0x005C254D:
 *
 *     005C254D  mov byte [edi+0xce0], 1
 *
 * a missing dongle reads as authenticated. It does not survive, because
 * 0x005C1ED0 - the cabinet check that also counts I/O boards and validates the
 * serial - runs later, finds [edi+0xcdc] already zero at 0x005C2160 and writes
 * zero over both bytes at 0x005C21F5.
 *
 * So set it where that function will see it: on the way in, before its own
 * first read. An earlier attempt set the same two bytes from the boot task's
 * update and was overwritten a few hundred instructions later by this very
 * function, which ES3_WATCH_MEM named.
 *
 * ES3_NO_DONGLE_OK leaves it alone, which is how to see the panel again.
 */
/* The two places the record lives. They are not always the same object - the
 * board handler next door prints both precisely because they differ on some
 * runs - so write whichever of them is still zero. */
static void mk_say_authenticated(uint32_t edi)
{
    uint32_t slot = rd32(0x00959B5Cu), via = slot ? rd32(slot) : 0;
    if (via && rd8(via + 0x0CDCu) == 0) {
        wr8(via + 0x0CDCu, 1);
        wr8(via + 0x0CE0u, 1);
    }
    if (edi && rd8(edi + 0x0CDCu) == 0) {
        wr8(edi + 0x0CDCu, 1);
        wr8(edi + 0x0CE0u, 1);
    }
}

static int mk_dongle_off(void)
{
    static int off = -1;
    if (off < 0) off = getenv("ES3_NO_DONGLE_OK") != NULL;
    return off;
}

static int mk_cabinet_authenticated(CPU *c)
{
    static int said;
    if (mk_dongle_off()) return 0;
    mk_say_authenticated(c->edi);
    if (!said) {
        said = 1;
        fprintf(stderr, "[board] there is no F:/dongle.bin and no drive to "
                        "put one on; saying the cabinet is authenticated, "
                        "which is what the game says itself when the dongle "
                        "cannot be read (ES3_NO_DONGLE_OK).\n");
    }
    return 0;                         /* the game's own check still runs */
}

/*
 * "Not yet" rather than "no".
 *
 * 0x00679470 answers yes-there-is-a-problem when the ALL.Net client object at
 * [0x0095A894] is null, and 0x005BF4D0 asks it once, a second or two into the
 * boot - before the client has finished resolving, connecting and posting its
 * PowerOn. It gets yes, files E05-55, and puts the boot task into state 3,
 * which nothing moves it out of. The client comes up immediately afterwards
 * and has nowhere to report it.
 *
 * So the only thing held back is the answer given while the object does not
 * exist yet. Once it does, the game's own test stands - including its verdict
 * on [0x0095A850]+0x94, which now reads 0 because the authentication really
 * did happen. This is a race being waited out, not a network being faked; the
 * faking lives in mk_boot_net_state and is off by default.
 *
 * ES3_NO_NET_WAIT hands the question straight back.
 */
static int mk_net_ok(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_NO_NET_WAIT") != NULL;
    if (off) return 0;                /* not handled: the game's own runs */
    if (!mk_dongle_off()) mk_say_authenticated(0);
    if (rd32(0x0095A894u)) return 0;  /* the client is up; ask it, not us */
    if (!said) {
        said = 1;
        fprintf(stderr, "[net] the boot asked about the network before the "
                        "All.Net client existed; saying not-a-problem until "
                        "it does (ES3_NO_NET_WAIT to answer honestly).\n");
    }
    c->eax = 0;                       /* al = 0: no problem */
    c->esp += 4;                      /* the return address, as `ret` would */
    return 1;
}

/*
 * The cabinet's network state, set where the boot task is about to read it.
 *
 * 0x005BF340 is the boot task's update. Its state is [this+0x60], and in
 * state 1 it reaches 0x005BF516, which is where the boot either continues or
 * stops for good:
 *
 *     005BF516  cmp dword [ecx+0x90], 0x67   ; All.Net said OK
 *     005BF51D  jne ...
 *     005BF52A  cmp byte [[0x959B5C]+0xCDC], 0
 *     005BF530  jne 0x5bf5c5                 ; and authenticated: carry on
 *     005BF53C  push 0x38                    ; otherwise E05-55, and
 *     005BF549  mov [edi+0x60], 3            ; state 3 - the offline screen
 *
 * Nothing ever moves the state back out of 3, so the whole boot turns on one
 * evaluation, a few seconds in. That is why holding these fields down from
 * the watchdog at ten hertz never worked: by the time a poke resolves the
 * chain, the decision has been taken and the state is 3 for ever.
 *
 * A pre-hook does not have that problem. Returning 0 leaves the original to
 * run - see es3_guest_hle_run() - so this sets the three fields the test is
 * about to read, on the same call, every time.
 *
 * What it says is what a cabinet on a working network would have: the last
 * All.Net exchange returned 0x67, there is no pending error, and the
 * authentication record is present. allnet.c is what would produce that if
 * the client ever posted its PowerOn; it does not, and this stands in.
 *
 * ES3_NO_NET_OK leaves all of it alone.
 */
#define ALLNET_OK 0x67u

static int mk_boot_net_state(CPU *c)
{
    static int off = -1, said;
    uint32_t client, slot, obj;

    if (off < 0) off = getenv("ES3_FAKE_NET_OK") == NULL;
    if (off) return 0;

    client = rd32(0x0095A850u);
    if (client) {
        if (rd32(client + 0x90u) != ALLNET_OK) {
            wr32(client + 0x90u, ALLNET_OK);   /* the PowerOn result */
            wr32(client + 0x94u, 0);           /* and no pending error */
            if (!said) {
                said = 1;
                fprintf(stderr, "[net] telling the boot the network is up: "
                                "All.Net result 0x67, cabinet authenticated "
                                "(ES3_NO_NET_OK to leave it).\n");
            }
        }
    }

    slot = rd32(0x00959B5Cu);
    obj = slot ? rd32(slot) : 0;
    if (obj) {
        wr8(obj + 0x0CDCu, 1);                 /* authenticated, and */
        wr8(obj + 0x0CE0u, 1);                 /* the flag it is copied from */
    }
    return 0;                                  /* the game's own update runs */
}

int main(int argc, char **argv)
{
    CPU cpu;

    /* Before anything else, so the child does the work and this process only
     * watches. Does nothing unless ES3_DEBUG is set, and never returns in the
     * parent when it is. */
    es3_debug_self();

    if (argc < 2) {
        fprintf(stderr,
            "usage: %s <MK_AGP3_FINAL.exe>\n"
            "\n"
            "  The executable from a game tree you can already read. None of it\n"
            "  ships here - see the README. Run it from the game tree's own\n"
            "  directory: the game opens Data\\ and DataGlobal\\ by relative\n"
            "  path and will not find them from anywhere else.\n",
            argv[0]);
        return 2;
    }

    /* Unbuffered, because the interesting runs are the ones that die. A
     * redirected stdout is block-buffered, so a fault takes the whole log with
     * it and the last thing you see is from several thousand calls earlier. */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    if (guest_load(argv[1]) != 0)
        return 1;

    es3_install_crash_handler();

    /* Give the imports bodies. hle_register_all() forwards everything the host
     * has a DLL for and reports what is left, which is the board. */
    hle_register_all();

    /* Guest functions this runtime answers itself - the cabinet, asked
     * for from inside the game rather than through a DLL. */
    es3_bind_guest(0x005C37E0u, mk_trace_error);
    es3_bind_guest(0x005C2C50u, mk_trace_raise);
    es3_bind_guest(0x005C1ED0u, mk_cabinet_authenticated);
    es3_bind_guest(0x0073ECF0u, mk_camera_present);
    es3_bind_guest(0x005BEA80u, mk_camera_off);
    es3_bind_guest(0x007A8590u, mk_io_board_count);
    es3_bind_guest(0x00679470u, mk_net_ok);
    es3_bind_guest(0x005BF340u, mk_boot_net_state);

    guest_init_cpu(&cpu);
    es3_watch_cpu(&cpu);

    printf("[host] entering %s at %#010x (image at %#010x)\n",
           argv[1], guest_entry(), guest_image_base());

    /* The guest does not return here: its exit path is ExitProcess, which the
     * forwarded kernel32 performs for real. Reaching the line below means
     * mainCRTStartup returned, which it does not do. */
    es3_enter_guest(&cpu);

    fprintf(stderr, "[host] the entry point returned - mainCRTStartup should not.\n");
    return 1;
}
