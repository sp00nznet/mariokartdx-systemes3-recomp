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

#include "es3_rt.h"

int main(int argc, char **argv)
{
    CPU cpu;

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

    if (guest_load(argv[1]) != 0)
        return 1;

    /* Give the imports bodies. hle_register_all() forwards everything the host
     * has a DLL for and reports what is left, which is the board. */
    hle_register_all();

    guest_init_cpu(&cpu);

    printf("[host] entering %s at %#010x (image at %#010x)\n",
           argv[1], guest_entry(), guest_image_base());

    /* The guest does not return here: its exit path is ExitProcess, which the
     * forwarded kernel32 performs for real. Reaching the line below means
     * mainCRTStartup returned, which it does not do. */
    dispatch(&cpu, guest_entry());

    fprintf(stderr, "[host] the entry point returned - mainCRTStartup should not.\n");
    return 1;
}
