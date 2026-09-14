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
#include <string.h>

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

    guest_init_cpu(&cpu);
    es3_watch_cpu(&cpu);

    printf("[host] entering %s at %#010x (image at %#010x)\n",
           argv[1], guest_entry(), guest_image_base());

    /* The guest does not return here: its exit path is ExitProcess, which the
     * forwarded kernel32 performs for real. Reaching the line below means
     * mainCRTStartup returned, which it does not do. */
    dispatch(&cpu, guest_entry());

    fprintf(stderr, "[host] the entry point returned - mainCRTStartup should not.\n");
    return 1;
}
