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
| Functions recovered | see below — the binary is stripped, so these are recovered, not read |
| Imports | **495**, of which **489** have a derived stack purge |
| Board imports | **40** — the OKAO Vision camera, entirely by ordinal |
| Runs | Not yet. The board needs bodies, and nothing is guessed. |

### The hard part is not the CPU

It is worth being clear about where the work actually is, because it is not
where a console recomp's is.

The host **is** the machine. A System ES3 is an Intel desktop running Windows;
this is x86-32 code that wants kernel32 and Direct3D, and the computer you
build on has both. `systemes3recomp` forwards the import call to the real
function — `CreateFileW` is answered by `CreateFileW` — so roughly 455 of the
495 imports need no work at all.

What is left is the cabinet:

| | imports | |
|---|---:|---|
| `eOkao*.dll` ×5 | 40 | OMRON OKAO Vision — the camera in the roof. Face detection, facial parts, age and gender estimation. Imported **purely by ordinal**, documented nowhere, and the DLLs exist nowhere but in a game tree. |
| JVS I/O | via `DeviceIoControl` | Coins, wheel, pedals, item button, service and test. The `v1.06.35` build reaches it through `JVSEmuMK.dll!engate` instead, which is the clearest look at the interface we have. |
| `bngrw.dll` | 11 (v1.18+) | The Bandai Namco card reader. Progress, licence and kart to a magnetic card. |
| `Nbam_QR_Code.dll` | 6 (v1.18+) | The QR code printed on that card. Pure computation — the easiest of these. |
| AMCUS / Mucha | — | Network authentication, over stock WinHTTP to servers that are gone. Forwarded, and it fails the way an unplugged cabinet failed. |

**None of these are stubbed**, and that is deliberate. A handler that returns 0
lets the game past the call and breaks it somewhere else an hour later; a
handler that aborts naming itself is the to-do list in the order the game wants
it. See [systemes3recomp's docs/board-io.md](https://github.com/sp00nznet/systemes3recomp/blob/main/docs/board-io.md).

### The camera is why the toolchain changed

A nice illustration of what an arcade target does to a PC toolkit. Every
import shim has to pop exactly what the real function popped, and the counts
are derived rather than typed — from the Windows SDK's import libraries, from
a `_Name@16` decoration, from a C++ mangling. All of those read the count off
a **name**.

OKAO Vision has no names. It exports by ordinal, there is no import library for
it anywhere on earth, and its DLLs ship only inside the game. 40 imports, no
answer, 40 aborts.

So [pcrecomp](https://github.com/sp00nznet/pcrecomp) learned to read the count
out of the callee: find the export, walk it to its first `ret N`, and there it
is. Checked against the SDK import libraries over every export where both have
an answer — **1,307 agree, 2 disagree** — and on this game's import table it
took the resolved count from **449 of 495 to 489 of 495**.

### The graphics are two APIs at once

The same binary imports from **both** `d3d9.dll` and `d3d10.dll`, plus
`d3dx9_43` and `d3dx10_43`, and picks at run time. Both get forwarded to the
host's real DirectX; which one the cabinet actually took is a measurement
nobody has made yet.

## Reproducing it

```powershell
git clone --recursive https://github.com/sp00nznet/mariokartdx-systemes3-recomp
cd mariokartdx-systemes3-recomp\systemes3recomp

# what the game asks the board for
py -3.11 -m tools pe path\to\MK_AGP3_FINAL.exe

# recover its functions. Slow - hours on this binary - and you only do it once.
py -3.11 -m tools scan path\to\MK_AGP3_FINAL.exe ..\catalog.json

# lift it
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

It will then abort on the first thing it wants that the host cannot answer,
naming it and the DLL it came from. That is the intended first run and the
whole work plan.

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
