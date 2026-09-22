# mariokartdx-systemes3-recomp

**Mario Kart Arcade GP DX (Namco / Nintendo, 2013) — a System ES3 kart racer,
statically recompiled from its Win32 executable to native C.**

A cabinet with a wheel, a camera in the roof that photographs you and puts your
face on your kart, a card reader that remembers you, and a network that no
longer answers. Underneath all of that it is a Visual Studio 2010 application
running on a desktop PC, which is exactly the kind of thing a static
recompiler should be good at.

Built on [**systemes3recomp**](https://github.com/sp00nznet/systemes3recomp),
the Namco System ES3 recompilation toolkit, vendored here as a git submodule.

> **No game data here.** No executable, no `Data\`, no DLLs, no certificates.
> The `.gitignore` refuses all of it. Bring a game tree you can already read;
> the recompiled C is output you generate.

![Mario Kart Arcade GP DX, recompiled: the last lap of a race, finishing first](docs/first-place.gif)

*The final lap of a race played on a gamepad, recompiled. Donkey Kong, lap
2/2, first place — and the finish. Not a demo loop: the steering, the item
and the coin are a real pad, and the race was won against the game's own AI.*

![First place under the Mario Kart arch](docs/first-place.png)

*The same run, a frame earlier. Attract mode used to be as far as this got;
the card prompt that stood in for a screenshot for months is now just
something you skip on the way to picking a character.*


## Why this target

Not because it is easy — it is the first ES3 title anyone has taken apart — but
because it is the one with a control group. Four separate builds exist,
spanning 2013 to 2022, and a change that works against one and not another is a
change that was fitted to a binary rather than to the platform. See
[docs/versions.md](docs/versions.md).

## Status

```
$ py -3.11 -m tools pe MK_AGP3_FINAL_v1.00.32.exe
format     PE32 i386
image      0x00400000 .. 0x0081cd7c
entry      0x007cb996
code       0x00401000 + 0x41bd7c
linker     MSVC 10.00   built 2013-04-23 12:10:51 UTC
sections   .text .rdata .data .rsrc .reloc
imports    495 functions from 27 DLLs
  needs    MSVCR100.dll         180
  needs    KERNEL32.dll          94
  needs    USER32.dll            60
  needs    WS2_32.dll            29
  needs    d3dx10_43.dll         16
  needs    eOkaoDt.dll           13   <- OMRON OKAO Vision - face detection
  needs    d3dx9_43.dll          11
  needs    eOkaoPt.dll           10   <- OMRON OKAO Vision - facial parts
  needs    WINHTTP.dll           10
  ...
```

| | |
|---|---|
| Target build | **v1.00.32**, the 2013 Japanese release — the smallest and the only unmodified one of the four |
| PE parsing | **Works** on all four builds |
| Functions recovered | **25,757** — the binary is stripped, so these are recovered by recursive descent, not read |
| Functions lifted | **29,645**, into 75 translation units. Not one failed outright. More than the catalog holds, because the driver closes every address the *generated text* dispatches to — a fall-through past a clamped extent, an arm of a jump table — round after round until nothing is left open |
| Instruction coverage | **Complete for every path this game takes.** The three it stopped on this round were `cvtps2pd`, `fldln2` and `fyl2x`, each hidden behind the last, and all of them went upstream into pcrecomp along with the rest of the x87 transcendental set |
| Imports | **495 of 495** resolved against real DLLs when run from a game tree — the OKAO Vision camera and `JVSEmuMK.dll` ship with the game, so the cabinet's own libraries answer for themselves |
| Board imports | **40** — the OKAO Vision camera, entirely by ordinal |
| Builds | **Yes** — every translation unit to a native executable |
| Boots | **Yes, all the way through.** The JVS-injection entry stub, the CRT, every C++ static initialiser, a twenty-thread worker pool, a real `mkart3` window, Direct3D 10 and a DXGI swap chain, DirectInput 8 — and then the cabinet's own startup: drive unit, I/O board, NAMCAM, steering, IC card reader, local network, ALL.Net authentication. **No error filed in any of the five slots** |
| Renders | **Yes**, 1360x768 — the course, the karts, the characters, the HUD |
| Plays | **Yes, on a gamepad.** Coin in, pick a character, pick a track, steer, use items, finish a race and win it. The steering outlasted the renderer by weeks: the wheel value was correct the whole time while the game quietly threw it away in favour of a cabinet counter nothing here drives |
| Not finished | The pedals — the triggers now reach the game at full travel, but nothing consumes them yet and the kart runs on auto-accel. Four theories measured dead, written up in [docs/pedals.md](docs/pedals.md). The in-race camera sometimes hangs when it presents the player — intermittently, not every race, and the race carries on behind it when it does |

**How it got here** — the bring-up, in the order it happened, including the
wrong turns: [docs/bringup.md](docs/bringup.md). It is long because the
interesting part of a recomp is never the CPU.

## Reproducing it

```powershell
git clone --recursive https://github.com/sp00nznet/mariokartdx-systemes3-recomp
cd mariokartdx-systemes3-recomp\systemes3recomp

# what the game asks the board for
py -3.11 -m tools pe path\to\MK_AGP3_FINAL.exe

# recover its functions. Half an hour on this binary, and you do it once -
# every later step reads the catalog.
py -3.11 -m tools scan path\to\MK_AGP3_FINAL.exe ..\catalog.json

# lift it - minutes, and 199 MB of C comes out
py -3.11 -m tools recomp path\to\MK_AGP3_FINAL.exe ..\catalog.json ^
                         ..\games\mariokartdx\generated

# build - 32-bit and MSVC, and the CMake will not let you forget either
cd ..
cmake -S . -B build -A Win32
cmake --build build --config Release
.\build\Release\mariokartdx.exe path\to\MK_AGP3_FINAL.exe
```

Run it from the game tree's own directory: the game opens `Data\` and
`DataGlobal\` by relative path.

**And scan, lift and run the same file.** A tree often ships more than one
executable, and they are not interchangeable even when they look it: this one
has `MK_AGP3_FINAL.exe` and `MK_AGP3_FINAL_v1.00.32.exe`, the same size and
thirty-three bytes apart, with one of the differences at the entry point - the
patched build jumps to a stub that loads the cabinet's I/O emulator and the
clean build does not. The lifted C is one file's instructions and reads the
other file's constants, and the result runs without ever looking wrong. The
driver fingerprints what it lifted from and `guest_load()` checks it, so a
mismatch is now a paragraph at startup rather than a week.

The first run looks like this, and is meant to:

```
[hle] 455 imports forwarded to the host's own DLLs, 40 not found
      The ones left are the cabinet: JVS I/O, the card reader, the
      camera, the authentication. See docs/board-io.md.
[board] eOkaoAg.dll  unimplemented  <- OMRON OKAO Vision - age estimation
[board] eOkaoCo.dll  unimplemented  <- OMRON OKAO Vision - common
[board] eOkaoDt.dll  unimplemented  <- OMRON OKAO Vision - face detection
[board] eOkaoGn.dll  unimplemented  <- OMRON OKAO Vision - gender estimation
[board] eOkaoPt.dll  unimplemented  <- OMRON OKAO Vision - facial parts
[host] entering MK_AGP3_FINAL.exe at 0x007cb996 (image at 0x00400000)
```

It then aborts on the first thing it wants that the host cannot answer, naming
it and the DLL it came from. That is the whole work plan, in the order the game
wants it.

## Layout

```
systemes3recomp/                 the toolkit (git submodule)
games/mariokartdx/
  src/host.c                     load the PE, enter it, get out of the way
  generated/                     the lifted C (you generate it; gitignored)
docs/versions.md                 the four builds, measured
```

## Legal

The host and the recompilation toolchain are original work, MIT-licensed.
**No game data, no executables, no DLLs, no certificates, and no circumvention
of anything.** *Mario Kart Arcade GP DX* is © Nintendo and Bandai Namco; this
is an independent, non-commercial preservation project. Built on
[**systemes3recomp**](https://github.com/sp00nznet/systemes3recomp).
