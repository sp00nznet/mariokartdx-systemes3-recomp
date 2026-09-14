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
    es3_bind_guest(0x007A8590u, mk_io_board_count);

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
