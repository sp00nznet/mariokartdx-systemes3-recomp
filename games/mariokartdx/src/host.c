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
    /*
     * Off unless asked for, because claiming a camera that is not there is
     * worse than having none.
     *
     * The object at [0x00959B4C] is eight bytes built as { interface = NULL,
     * present = 0 } at 0x006AAD7F. The real 0x0073ECF0 builds a capture graph
     * and sets BOTH. This only ever set the present byte, so the game was
     * told a camera existed and handed a null interface to reach it with -
     * and it believed the byte. The first time anything asked, on the frame a
     * game actually starts, it faulted in 0x0069EB20.
     *
     * It also contradicted mk_camera_off(), which tells the boot the camera
     * is not fitted. Two stand-ins for one piece of hardware, disagreeing.
     *
     * Letting the real function run is the honest answer and the working one:
     * it looks for a camera, does not find one, and leaves present = 0, which
     * is a state the game has a checklist line for and handles everywhere.
     * With this off a race starts and ends cleanly; with it on the process
     * died every time credits were satisfied.
     */
    if (off < 0) off = getenv("ES3_CAMERA_PRESENT") == NULL;
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
 * "This is a cabinet boot", which is what decides whether the board is
 * authenticated at all.
 *
 * 0x004636A0 is the boot's All.Net step, and its first test is one byte:
 *
 *     004636D9  cmp byte [[0x00959B1C]+3], 0
 *     004636DC  jne 0x4636f2                   ; a cabinet: authenticate
 *     004636E5  call log("...no board authentication")
 *     004636EA  mov byte [esi], 1              ; and mark it done
 *
 * and the string it logs says exactly what the zero means:
 * "筐体起動ではないので基板認証なし" - not a cabinet boot, so no board
 * authentication. With it zero the whole All.Net exchange is skipped, the boot
 * task never reaches 0x005BF4D0, and the cabinet sits in <OFFLINE OPERATION>
 * on the operator menu for ever. Nothing in this runtime was ever asked about
 * the network, which is why none of the hooks that answer it printed anything.
 *
 * This machine IS standing in for a cabinet, so it says so - once, where the
 * byte is read, rather than as a poke held down ten times a second against a
 * field the game may want to change later.
 *
 * ES3_NOT_A_CABINET leaves it alone, which is how to get the operator menu
 * back.
 */
static int mk_cabinet_boot(CPU *c)
{
    static int off = -1, said;
    uint32_t cfg;

    (void)c;
    if (off < 0) off = getenv("ES3_NOT_A_CABINET") != NULL;
    if (off) return 0;
    cfg = rd32(0x00959B1Cu);
    if (cfg && rd8(cfg + 3u) == 0) {
        wr8(cfg + 3u, 1);
        if (!said) {
            said = 1;
            fprintf(stderr, "[net] the boot was about to skip board "
                            "authentication because this is not a cabinet; "
                            "saying it is (ES3_NOT_A_CABINET).\n");
        }
    }
    return 0;                         /* the game's own step still runs */
}

/*
 * Did attract mode start? Two addresses that say so, and have never fired.
 *
 * 0x006A5140 builds the attract scene - it reads [0x0095A87C], the cabinet
 * link object, at 0x006A5196 and constructs clAttractDemoLoader at 0x006A51D6.
 * 0x005C4A80 is the loader's own entry, and the game logs "AttractType : [%d]"
 * from 0x005C4AAE when it runs.
 *
 * No log this port has ever produced contains the word Attract, so neither of
 * them has been reached. They are bound so that the moment one is, it says so
 * - which is the difference between "attract mode is not drawing" and "attract
 * mode was never asked for", and every hour spent on the first of those while
 * it was really the second is an hour wasted.
 *
 * ES3_NO_ATTRACT_TRACE turns the two lines off.
 */
static int mk_attract_note(CPU *c)
{
    static int off = -1, said_scene, said_load;
    uint32_t from = rd32(c->esp);
    (void)from;
    if (off < 0) off = getenv("ES3_NO_ATTRACT_TRACE") != NULL;
    if (off) return 0;
    if (!said_scene) {
        said_scene = 1;
        fprintf(stderr, "[attract] the attract scene is being built\n");
    }
    (void)said_load;
    return 0;
}

static int mk_attract_load(CPU *c)
{
    static int said;
    if (!said && !getenv("ES3_NO_ATTRACT_TRACE")) {
        said = 1;
        fprintf(stderr, "[attract] clAttractDemoLoader started, type %u - "
                        "this is attract mode\n", c->eax);
    }
    return 0;
}

/*
 * The steering potentiometer, which is not wired to anything here.
 *
 * E23-01, STEERING VOLUME DEVICE ERROR, and like every other cabinet error it
 * is a mode whose entry at 0x00871A10 begins with 1 - so filing it stops the
 * frame loop's task tick and the game draws nothing.
 *
 * 0x005BEF80 is the steering task and it has the camera's shape exactly:
 *
 *     005BEF8F  cmp dword [edi+0x54], 2      ; the check is complete
 *     005BEF94  cmp dword [edi+0xad8], 0     ; and the device is fitted
 *     005BF0E0  cmp dword [edi+0x70], 0x384  ; nine hundred frames
 *     005BF0E9  cmp dword [edi+0x54], 0      ; nothing decided yet
 *     005BF0FB  push 0x61                    ; then E23-01
 *
 * The sub-state it is waiting on is advanced by 0x005BDCC0, which only says
 * yes when the drive board has answered - and the drive board never speaks
 * (see mk_drive_board_connected). So say what is true: the check is complete
 * and the potentiometer is not fitted. That is the same answer mk_camera_off
 * gives, through the same two fields, and it leaves the boot with a checklist
 * line rather than an error that stops everything.
 *
 * ES3_STEERING_ON leaves it to the serial port.
 */
static int mk_steering_off(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_STEERING_ON") != NULL;
    if (off || !c->ecx) return 0;
    if (rd32(c->ecx + 0x54u) != 2u) {
        /* Complete, and PRESENT - not "not fitted".
         *
         * [this+0xAD8] zero takes 0x005BEFE0, which renders the string at
         * 0x0088AC20: "OFF". A cabinet that reports its wheel as absent is a
         * cabinet nobody can play, and the boot draws a crossed-out steering
         * wheel in the corner to say so. Above zero takes 0x005BEFA4 instead,
         * which renders 0x0088ABEC - "OK" - as long as [[0x00959B38]+0x1A0] is
         * zero, and it is measured zero here. ES3_STEERING_OFF goes back to
         * OFF for comparison. */
        wr32(c->ecx + 0x54u, 2u);     /* the check is complete */
        wr32(c->ecx + 0xAD8u,
             getenv("ES3_STEERING_OFF") ? 0u : 1u);
        if (!said) {
            said = 1;
            fprintf(stderr, "[wheel] there is no steering potentiometer on "
                            "this machine; telling the boot the check is done "
                            "and it is not fitted (ES3_STEERING_ON).\n");
        }
    }
    return 0;                         /* the game's own task still runs */
}

/*
 * The IC card vendor, which is the check the boot actually stops on.
 *
 * With the checklist printed as text (mk_trace_check) the list ends after the
 * line at y=364 and nothing ever posts the next one, at y=390 - which on
 * screen is IC CARD VENDOR CHECK, the row that sits there animating while the
 * rest of the boot waits.
 *
 * 0x005BF120 owns that row and has the shape every one of these has, with one
 * difference worth writing down: its state is at [this+0x58], not [this+0x54]
 * like the check above it. It posts "OK" (0x0088ABEC) when the state is 2 and
 * [this+0xAD8] is above zero, and "OFF" (0x0088AC20) when it is not; until the
 * state reaches 2 it posts nothing at all, which is the hang.
 *
 * Nothing can move that state here. The vendor is a serial device on COM2 or
 * COM4, and this runtime deliberately stopped answering those ports - jvs.c
 * was replying to the card reader in JVS, which is what E07-11 was. So the
 * honest answer is the same one the camera and the wheel get: the check is
 * complete, and the device is there.
 *
 * ES3_NO_CARD_VENDOR leaves it to the serial port, which is how to watch the
 * boot stop here again.
 */
static int mk_card_vendor_done(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_NO_CARD_VENDOR") != NULL;
    if (off || !c->ecx) return 0;
    if (rd32(c->ecx + 0x58u) != 2u) {
        wr32(c->ecx + 0x58u, 2u);     /* the check is complete */
        wr32(c->ecx + 0xAD8u, 1u);    /* and the vendor is there */
        /* And hand on to the next step, which this shortcut would otherwise
         * strand. 0x005BF120 only writes [+0x5C] on the path it takes when
         * [+0x58] is not yet 2 (at 0x005BF17A and 0x005BF1D4); setting 0x58
         * here means it takes the "already done" branch instead and the
         * vendor R/W step after it never leaves whatever state it started
         * in. That is one whole row of the checklist, and the boot sits on
         * it for ever. */
        wr32(c->ecx + 0x5Cu, 1u);     /* ask the vendor R/W step */
        wr32(c->ecx + 0x70u, 0u);     /* with its timeout counter reset */
        if (!said) {
            said = 1;
            fprintf(stderr, "[card] the IC card vendor is on a serial port "
                            "nothing answers; saying its check is done "
                            "(ES3_NO_CARD_VENDOR).\n");
        }
    }
    return 0;                         /* the game's own task still runs */
}

/*
 * IC CARD VENDOR R/W CHECK, the row the boot actually sits on.
 *
 * 0x005BF220 is that step, and it is a three-state machine on [this+0x5C]:
 * 1 is "ask", 2 is "done", 3 is "in progress" - and 3 draws its row inline
 * instead of calling 0x005BF900, which is why the trace above never showed
 * y=442 and I spent a while believing the row was missing. It is not
 * missing. It says PROGRESS for ever.
 *
 * The ask, at 0x005BF2D3, is 0x00678D30: "is the vendor link up?" It is true
 * only when [0x00952827] and [0x0095A87C] are both set, and 0x0095A87C is the
 * AMNet session handle, which nothing here ever hands out. So the step falls
 * through to 0x006789E0, which opens a socket to a vendor that does not
 * exist, returns 0, and latches state 3.
 *
 * 0x00678D30 has exactly one caller - this check - so answering it is the
 * whole fix, and it costs the network phase nothing: state 1 then takes the
 * 0x005BF320 path, which is the same "done" the real cabinet reaches.
 */
static int mk_vendor_link_up(CPU *c)
{
    static int off = -1;
    if (off < 0) off = getenv("ES3_NO_CARD_VENDOR") != NULL;
    if (off) return 0;
    c->eax = 1u;                      /* the vendor link is up */
    c->esp += 4;                      /* consume the return address */
    return 1;
}

/*
 * UPDATE CHECK, the last row, and the last thing between here and the game.
 *
 * 0x005BF730 is a state machine on [this+0x68] like the vendor step, and its
 * state 2 is "an update is downloading" - drawn inline at y=0x256, left for
 * the updater thread to finish, and on a board with no update server that
 * thread never does. It enters state 2 from state 1 at 0x005BF7DF, on
 * [[0x00959B38]+0x190]: an update is pending.
 *
 * Nothing here can serve one, so the answer is no. With the byte clear the
 * step takes 0x005BF80D instead, asks 0x00679470 - which this file already
 * answers - and writes state 3, complete.
 *
 * The byte is clear when the vendor step reads it at 0x005BF2E1 and set by
 * the time this one does, so it is the network phase that sets it, which is
 * the phase this port only reached once the vendor row stopped stalling.
 *
 * ES3_UPDATE_WAIT leaves it alone, which is how to watch the boot sit on
 * PROGRESS here again.
 */
static int mk_no_update(CPU *c)
{
    static int off = -1, said;
    uint32_t sys;

    (void)c;
    if (off < 0) off = getenv("ES3_UPDATE_WAIT") != NULL;
    if (off) return 0;
    sys = rd32(0x00959B38u);
    if (sys && rd8(sys + 0x190u)) wr8(sys + 0x190u, 0);

    /*
     * And the download itself, which is the route this boot actually takes.
     *
     * With no update pending the step goes to 0x005BF80D, asks 0x00679470 -
     * which says the network is fine, because this file made it fine - and
     * arrives at 0x005BF84F, the real update logic. There it reads one slot
     * of the download table, [[[0x00959B60]+8]+0x48], and:
     *
     *     005BF85A  cmp dword ptr [edx + 0xc], 6
     *     005BF85E  je  0x5bf8b6            ; finished: the check completes
     *     005BF865  call 0x663980           ; else: has it timed out?
     *     005BF887  mov dword ptr [esi + 0x68], 2   ; no: start downloading
     *
     * State 2 draws PROGRESS inline and waits for a download from a server
     * that is a hundred lines of C in allnet.c. 6 is the game's own value for
     * a slot that has finished, so say the slot has finished - which is true:
     * there is no update to fetch and nothing is going to arrive.
     *
     * Only while this step is still asking ([this+0x68] == 1), so the field is
     * not held down over whatever the downloader does with it afterwards.
     */
    if (c->ecx && rd32(c->ecx + 0x68u) == 1u) {
        uint32_t tbl = rd32(0x00959B60u);
        uint32_t slot = tbl ? rd32(rd32(tbl + 8u) + 0x48u) : 0;
        if (slot && rd32(slot + 0xCu) != 6u) {
            wr32(slot + 0xCu, 6u);
            if (!said) {
                said = 1;
                fprintf(stderr, "[upd] the boot is about to download an "
                                "update from a server that is not there; "
                                "saying the download is finished, which it "
                                "is (ES3_UPDATE_WAIT).\n");
            }
        }
    }
    return 0;                         /* the game's own step still runs */
}

/*
 * Free play lives in a file, not in this process.
 *
 * The cabinet's operator settings are sv/TestMode/testmode_<unixtime>.bin in
 * the game tree, 168 bytes, and the layout is readable because 0x00667B50
 * names every field as it walks them: +0xE8 language, +0xEC card_charge,
 * +0xF0 game_charge, +0xF4 continue_charge, +0xF8 freeplay_setup, +0xFC
 * icrw_setup, +0x100 icvender_setup, +0x104 steer_setup, the two namcam
 * settings, three analog calibrations, two volumes, then +0x128..+0x144
 * monday..everyday, and on to +0x174 coin_num and +0x178
 * service_switch_num.
 *
 * In the file those are little-endian dwords starting at offset 2, in that
 * same order, so freeplay_setup is entry 7 at offset 0x1E. The alignment is
 * not a guess: entry 19..26 are eight copies of 1440 - minutes in a day, the
 * weekly closing times - and entry 30 and 31 are C0A80000 and FFFFFF00, an
 * address and a netmask.
 *
 * Bytes 0..1 are CRC-16/ARC over bytes[2:-2] and the last two bytes are its
 * one's complement, which is why editing the file needs the checksum redone:
 * D625 and 29DA sum to FFFF on the original.
 *
 * Nothing here does that. An earlier version of this file resolved the
 * settings through the singleton at [0x00959B60] and wrote freeplay_setup
 * once a frame, and it was writing to four objects that hold nothing - every
 * field of all four reads zero at any point in a boot, so they are not what
 * the game consults. The settings the game uses come from that file, and the
 * honest way to change an operator setting is to change the operator's
 * setting.
 */

/*
 * A coin, when you press one in.
 *
 * The first version of this pinned the credit count at whatever the game was
 * asking for, every frame. It worked - CREDIT(S) went 00/02 to 02/02 - and it
 * is not a coin slot: the count never goes down, so the game can never spend
 * a credit, and an attract loop with credits permanently available is a
 * cabinet that is always mid-transaction. Runs with it pinned crashed; runs
 * with ES3_NO_CREDITS exited cleanly.
 *
 * So take the coin edge the runtime already has - keyboard 5, or either
 * stick click on a pad - and add one credit per press, the way a coin
 * mechanism does. The counters are the ones the task that calls itself
 * "CreditChk." compares:
 *
 *     006A546C  mov edx, [ecx + 0x1c]   ; ecx = [[0x00959B38]+4], credits held
 *     006A547C  mov ecx, [esi + 8]      ; esi = [0x00959B38], credits needed
 *     006A54C4  cmp dword ptr [ebp - 0x10], eax
 *     006A54C7  jae 0x6a54f0            ; enough of them: get on with it
 *
 * The game does the deciding and the spending; this only puts coins in.
 *
 * ES3_FREE_CREDITS=n starts the cabinet with n credits already in it, for
 * getting to a screen quickly without reaching for the pad.
 */
static int mk_credits(CPU *c)
{
    static int started;
    uint32_t sys, credit, have;
    unsigned coins;

    (void)c;
    sys = rd32(0x00959B38u);
    if (!sys) return 0;
    credit = rd32(sys + 4u);
    if (!credit) return 0;

    coins = es3_coin_take();

    if (!started) {
        const char *e = getenv("ES3_FREE_CREDITS");
        started = 1;
        if (e) coins += (unsigned)atoi(e);
        fprintf(stderr, "[coin] the cabinet wants %u credit(s) per play. The "
                        "coin slot is the 5 key, or a click of either stick "
                        "on a pad (ES3_FREE_CREDITS=n to start with some).\n",
                rd32(sys + 8u));
    }

    if (coins) {
        have = rd32(credit + 0x1Cu) + coins;
        if (have > 99u) have = 99u;
        wr32(credit + 0x1Cu, have);
        fprintf(stderr, "[coin] %u coin(s) in; the cabinet now holds %u "
                        "credit(s) and wants %u.\n",
                coins, have, rd32(sys + 8u));
    }
    return 0;
}

/*
 * One cabinet, and it is this one.
 *
 * Mario Kart DX banks up to four cabinets on a LAN. 0x00676A20 answers how
 * many of them are talking: it walks the four peer slots at [this+0x12194]
 * and counts those whose [+0xC] and [+0xD] are both set. On a desk nothing
 * ever sets them, so the answer is zero - and zero is not the same as one:
 *
 *     0067618C  call 0x676a20
 *     00676191  cmp  eax, 1
 *     00676197  jne  0x6761a2       ; not one: wait, and time out at 60
 *     00676199  mov  byte [eax+0x103], 1   ; exactly one: get on with it
 *
 * The game already knows how to be a single cabinet. It just has to be told
 * it is one, and one is the true answer here: a standalone cabinet counts
 * itself and nobody else. Zero is the answer of a cabinet that cannot even
 * see its own link.
 *
 * Its other two callers draw the operator overlay that prints
 * "number of drive cabinets communicating <%d>", which will now say 1.
 *
 * ES3_NO_CABINET_LINK hands the question back, and then the count is zero
 * and every race waits out the timeout first.
 */
static int mk_one_cabinet(CPU *c)
{
    static int off = -1, said;
    if (off < 0) off = getenv("ES3_NO_CABINET_LINK") != NULL;
    if (off) return 0;
    if (!said) {
        said = 1;
        fprintf(stderr, "[link] the game asked how many drive cabinets are "
                        "talking; saying one, which is this one "
                        "(ES3_NO_CABINET_LINK to answer honestly with none).\n");
    }
    c->eax = 1;
    c->esp += 4;                      /* the return address, as `ret` would */
    return 1;
}

/*
 * Did DirectInput find the controller?
 *
 * 0x00740090 has two exits that both return 1, and they mean opposite
 * things: with no IDirectInput8 at [0x00952CB4] it sets a bit and leaves,
 * and with one it enumerates DI8DEVCLASS_GAMECTRL. Telling them apart from
 * outside is impossible and the difference is the whole question, so say
 * which at the point it is decided.
 */
static int mk_dinput_note(CPU *c)
{
    static int said;
    uint32_t di = rd32(0x00952CB4u);
    (void)c;
    if (!said) {
        said = 1;
        if (di)
            fprintf(stderr, "[dinput] the game has DirectInput at %08X and is "
                            "about to enumerate game controllers\n", di);
        else
            fprintf(stderr, "[dinput] the game has no DirectInput object, so "
                            "it will not look for a controller at all\n");
    }
    return 0;
}

/*
 * The PCB startup checklist, as text.
 *
 * 0x005BF900 does not draw anything - it appends one entry to the list the
 * startup screen renders: {string, colour, x, y}, five dwords at stride 20
 * from [this+0x7C]. Every check passes x = 0x1F4, so every VALUE lands in the
 * same column, and the labels are drawn wide enough to run straight into it.
 * On screen the result is a status covered by the label to its left, which is
 * unreadable exactly when it matters - it is the line that says which check
 * failed.
 *
 * So print it. The arguments are still on the stack at entry, before the
 * prologue: y at [esp+4], the string at [esp+8], the colour at [esp+0xC].
 * The string is UTF-16, and the colour is worth having because the game uses
 * 0x3B for a result it is happy with and 0x3F or 0x40 for one it is not.
 *
 * ES3_NO_CHECKLIST turns it off.
 */
static int mk_trace_check(CPU *c)
{
    static int off = -1;
    uint32_t str;
    if (off < 0) off = getenv("ES3_NO_CHECKLIST") != NULL;
    if (off) return 0;
    str = A32(1);

    /* Copied out a code unit at a time rather than handed to %ls.
     *
     * %ls abandons the whole conversion the moment one unit will not convert
     * in the current locale, and these strings are Japanese. The first line
     * that hit one printed no text AND no newline, so every entry after it ran
     * onto the same line - unreadable in exactly the place it was needed. */
    {
        char buf[128];
        unsigned i = 0;
        if (str) {
            const unsigned short *w = (const unsigned short *)(uintptr_t)str;
            for (; i + 1 < sizeof buf; i++) {
                unsigned ch = w[i];
                if (!ch) break;
                buf[i] = (ch >= 0x20 && ch < 0x7F) ? (char)ch : '.';
            }
        }
        buf[i] = 0;
        fprintf(stderr, "[check] y=%-4u x=%-4u colour=%-3u %s\n",
                A32(0), c->edx, A32(2), str ? buf : "(null)");
    }
    return 0;                         /* the game's own append still runs */
}

/*
 * The drive board, which is the other end of a serial cable that is not here.
 *
 * 0x005BEBD0 is the drive-unit task, and it waits the same way the camera did:
 *
 *     005BED29  cmp dword [ebx+0x70], 0x12c0   ; four thousand eight hundred
 *     005BED30  jbe 0x5bed5c                   ; frames, eighty seconds
 *     005BED3C  cmp byte [[edi+0x180]+0x49], 0 ; did the board ever answer
 *     005BED40  jne 0x5bed5c                   ; yes: no error
 *     005BED4E  push 0x60                      ; no: E22-12, STR PCB comms
 *
 * and E22-12, like E08-01 and E05-55 before it, is a mode whose entry in the
 * table at 0x00871A10 begins with 1 - so filing it stops the task tick and
 * nothing is drawn again.
 *
 * The byte at [board+0x49] is set by the serial driver when the board speaks.
 * It never does: ES3_TRACE_JVS shows the game opening \.\COM1 and posting a
 * three-byte overlapped read over and over without ever writing, so the board
 * is expected to talk first and jvs.c, which answers requests, has nothing to
 * answer. Standing in for the flag is the same trade as answering the I/O
 * board count: the honest answer on a machine playing the part of a cabinet is
 * that the board is there.
 *
 * The object arrives in ecx; the board hangs off the system object at
 * 0x00959B38, which is where 0x005BED32 reads it from.
 *
 * ES3_NO_DRIVE_BOARD leaves it to the serial port.
 */
static int mk_drive_board_connected(CPU *c)
{
    static int off = -1, said;
    uint32_t sys, board;

    if (off < 0) off = getenv("ES3_NO_DRIVE_BOARD") != NULL;
    if (off) return 0;
    sys = rd32(0x00959B38u);
    board = sys ? rd32(sys + 0x180u) : 0;
    if (board && rd8(board + 0x49u) == 0) {
        wr8(board + 0x49u, 1);
        if (!said) {
            said = 1;
            fprintf(stderr, "[board] the drive board never answered the serial "
                            "port; saying it is connected "
                            "(ES3_NO_DRIVE_BOARD).\n");
        }
    }
    return 0;                         /* the game's own task still runs */
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

    /*
     * The client's own verdict, once it has one.
     *
     * 0x00679470 is three tests, and the last of them is the byte at
     * [[0x0095A894]+0xDA4] - the client's running opinion of the network,
     * which 0x004678D0 sets from its argument when something goes wrong later.
     * The first two pass: the client exists and its status word at
     * [[0x0095A850]+0x94] reads 0, which is alAbExInit having succeeded and
     * the PowerOn having been answered.
     *
     * What sets the verdict afterwards is the keepalive against a server that
     * is a hundred lines of C in allnet.c, and it cannot be satisfied by
     * answering harder - the exchange it wants is not in the executable to
     * read. So clear it, and only while the authentication itself is good: if
     * the status word is non-zero the client has a real complaint and the
     * game's own answer stands.
     *
     * The alternative is to keep returning "no problem" from here, which hides
     * all three tests instead of the one. This way 0x00679470 still runs and
     * still decides.
     */
    {
        uint32_t client = rd32(0x0095A894u), sess = rd32(0x0095A850u);
        if (client && sess && rd32(sess + 0x94u) == 0 &&
            rd8(client + 0x0DA4u) != 0) {
            static int told;
            wr8(client + 0x0DA4u, 0);
            if (!told) {
                told = 1;
                fprintf(stderr, "[net] the All.Net client authenticated and "
                                "then decided the network was in trouble; "
                                "clearing its verdict, because the server it "
                                "is talking to is this runtime "
                                "(ES3_NO_NET_WAIT).\n");
            }
        }
    }

    {   /* The three inputs, once, so the answer is never a guess. */
        static int shown;
        uint32_t client = rd32(0x0095A894u), sess = rd32(0x0095A850u);
        if (!shown && client) {
            shown = 1;
            fprintf(stderr, "[net] 0x679470 reads: session %08X status %08X, "
                            "client %08X verdict %02X\n",
                    sess, sess ? rd32(sess + 0x94u) : 0xFFFFFFFFu,
                    client, rd8(client + 0x0DA4u));
        }
    }
    /*
     * And the answer itself, while the authentication is good.
     *
     * Clearing the verdict is not enough on its own: the boot task asks every
     * frame, 0x005BF4D0 goes to state 3 the first time it hears yes, and
     * nothing moves the task out of state 3 again. One frame in which the
     * client had set its verdict and this hook had not yet cleared it is the
     * whole run. So answer no while the session status at [[0x95A850]+0x94]
     * reads 0 - which is alAbExInit having succeeded and PowerOn having been
     * answered - and hand the question back the moment it does not.
     */
    {
        uint32_t sess = rd32(0x0095A850u);
        if (sess && rd32(sess + 0x94u) == 0) {
            static int told;
            if (!told) {
                told = 1;
                fprintf(stderr, "[net] the cabinet is authenticated; answering "
                                "the boot's network question with no, while "
                                "that stays true (ES3_NO_NET_WAIT).\n");
            }
            c->eax = 0;
            c->esp += 4;
            return 1;
        }
    }

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
    es3_bind_guest(0x005BEBD0u, mk_drive_board_connected);
    es3_bind_guest(0x005BEF80u, mk_steering_off);
    es3_bind_guest(0x005BF900u, mk_trace_check);
    es3_bind_guest(0x005BF120u, mk_card_vendor_done);
    es3_bind_guest(0x006A5140u, mk_attract_note);
    es3_bind_guest(0x005C4A80u, mk_attract_load);
    es3_bind_guest(0x004636A0u, mk_cabinet_boot);
    /* And 0x005BF220, the vendor R/W check, which reads the same byte at
     * 0x005BF2CD and skips the whole network phase when it is zero. ES3_POKE
     * was setting the byte at ten hertz and losing the race, which is the
     * mistake this file already has a paragraph about. */
    es3_bind_guest(0x005BF220u, mk_cabinet_boot);
    es3_bind_guest(0x00678D30u, mk_vendor_link_up);
    es3_bind_guest(0x007A8590u, mk_io_board_count);
    es3_bind_guest(0x00679470u, mk_net_ok);
    es3_bind_guest(0x005BF340u, mk_boot_net_state);
    es3_bind_guest(0x005BF730u, mk_no_update);
    es3_bind_guest(0x00740090u, mk_dinput_note);
    es3_bind_guest(0x005C38B0u, mk_credits);
    es3_bind_guest(0x00676A20u, mk_one_cabinet);

    /*
     * The controller, which the game asks DirectInput for itself.
     *
     * 0x004321F0 calls DirectInput8Create and 0x00740090 enumerates
     * DI8DEVCLASS_GAMECTRL, sets a DIJOYSTATE2 data format, takes the device
     * EXCLUSIVE|BACKGROUND on the game's own window and polls it at
     * 0x007405D0 with GetDeviceState into [obj+0x47C] - buttons at +0x4AC,
     * tested a byte at a time against 0x80. It is a complete gamepad path and
     * it is the game's, not a patch DLL's.
     *
     * All of it runs. What never ran was the enumeration callback, because
     * 0x007400DC pushes 0x0073FF60 - a guest address - straight into the real
     * DINPUT8.dll, which calls it as machine code. Planting a jump to its
     * thunk at that address is the whole fix; see es3_plant_callback().
     *
     * ES3_NO_DINPUT leaves it raw, which is how to watch the enumeration find
     * a controller and tell nobody.
     */
    if (!getenv("ES3_NO_DINPUT")) es3_plant_callback(0x0073FF60u);

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
