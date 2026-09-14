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
| Functions recovered | **26,075** — the binary is stripped, so these are recovered by recursive descent, not read. A first pass found 28,597 and 2,763 of those turned out to be addresses inside instructions |
| Functions lifted | **31,096**, into 2,633,954 lines of C across 78 translation units. Not one failed outright. More than the catalog holds, because the driver now closes every address the *generated text* dispatches to - a fall-through past a clamped extent, an arm of a jump table - round after round until nothing is left open |
| Instruction coverage | **99.966%** — 886 lines of 2.6 million are unlifted, and the game reaches none of them. The ones it did reach this round - `lock xadd`, `lock cmpxchg`, `cvtdq2ps` - went upstream into pcrecomp |
| Imports | **495**, of which **489** have a derived stack purge and **455** are answered by the host's own DLLs |
| Board imports | **40** — the OKAO Vision camera, entirely by ordinal |
| Builds | **Yes** — all 78 translation units to a native executable, no errors, no warnings |
| Boots | **Yes, into a frame loop.** The JVS-injection entry stub, the CRT, every C++ static initialiser, `CoInitialize`, its config off disk, a twenty-thread worker pool, a registered class, a real `mkart3` window with a working window procedure, **Direct3D 9Ex and Direct3D 10 both created**, its shader effects loaded through D3DX10, DirectInput 8 open, and D3DX10's thread pump feeding the loop through lifted callbacks |
| Imports | **495 of 495** resolved against real DLLs when run from a game tree — the OKAO Vision camera and `JVSEmuMK.dll` ship with the game, so the cabinet's own libraries answer for themselves |
| Renders | Not yet. The window is real, visible and the right size, and it is black: nothing is presented, and the process ends after about twenty-five seconds with no fault and nothing in the log |
| Plays | No. A window first, then the graphics stack, and nothing is guessed |

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

### Where it stops

```
[hle] 495 imports forwarded to the host's own DLLs, 0 not found
[game] *INF* Game Start
[hle] CreateWindowExW was given the guest's own image base as argument 10 -
      passing this process's module handle instead
[hle] DirectInput8Create was given the guest's own image base as argument 0 -
      passing this process's module handle instead
[r2l] real code called guest 005E64A0 directly (unthunked callback 1) - dispatching it
[r2l] real code called guest 007D9210 directly (unthunked callback 3) - dispatching it
```

That is a game that is running. It makes its window, brings up both renderers,
loads its effects, and runs a frame loop with D3DX10's thread pump calling the
game's own `ID3DX10DataLoader` methods on its worker threads - as lifted code.

What it does not do is put anything on the screen. The window is black,
`Present` is never reached, and after about twenty-five seconds the process
ends with no fault and no message, which is the signature of a forwarded CRT
calling `__fastfail` or of a thread deciding the boot has failed and calling
`ExitProcess`.

Finding out which is the next job, and the instruments are in the runtime:

```powershell
$env:ES3_TRACE_IMPORTS = "1"      # the first call to each import, in order
$env:ES3_TRACE_CALLS = "Present,CreateDeviceEx"   # arguments, result, last error
$env:ES3_WATCH_VA = "6ab300,4042c0"   # entered from where, which thread, returning what
.\mariokartdx.exe MK_AGP3_FINAL.exe
py -3.11 -m tools trail es3_trail.bin MK_AGP3_FINAL.exe   # every dispatch of the boot
```

`[game]` lines are the game's own `OutputDebugString`, which on the cabinet
went to a kernel debugger nobody was watching.

### Five things were in the way of the window, and only one was about windows

**The callback arena.** `hybrid`'s per-thread arena was reserved *and*
committed whole; seventeen threads at 64 MB is most of a 32-bit address space,
and the thread that lost got nothing - then returned 0 from every callback,
silently. A window procedure answering 0 to `WM_NCCREATE` is exactly
`CreateWindowExW` returning NULL and setting `ERROR_NOT_ENOUGH_MEMORY`.

**A jump table one arm short.** The window procedure's message switch has ten
arms; the lifter stopped walking at the first entry outside the function, and
arm nine is `WM_NCCREATE`.

**An extent ending mid-instruction.** `74 5B` is a two-byte `je`; read from its
second byte it is `pop ebx`. No fault, and the guest stack one slot out from
then on.

**The guest's own HINSTANCE.** `0x00400000` is a link-time constant, not a
module the loader knows. `DirectInput8Create` said `E_INVALIDARG`, the input
initialiser returned false, and every subsystem open after it was skipped -
which surfaced thirty thousand calls later as a task updating through a null
singleton.

**Real code calling guest code.** A window procedure is an argument and can be
thunked. A COM interface the game implements is not. So the guest image is now
mapped without execute, and an execute violation at a guest address is turned
back into a dispatch - one handler for every unthunked callback there will ever
be.

### What it cost the toolkit to get here

This is the first game anyone has put through pcrecomp's whole PC pipeline, and
every one of these was found by running it rather than by reading it. All fixed
[upstream](https://github.com/sp00nznet/pcrecomp), where they help every
PC-era target at once:

| what | found how |
|---|---|
| Function extents were not clamped to the next function, so 28,597 functions claimed **89.5 MB of bodies out of 4.3 MB of code** and lifted to 1.9 GB of C | the output was too big to compile |
| A body cut mid-function *returned* instead of transferring — skipping a `ret` that never ran and leaving esp four bytes low | reading the emitted C for a clamped function |
| `fucompp` and `fucomp` were **81% of every instruction the lifter could not express**, because only the ordered `fcom` forms were listed | counting the `abort()` lines |
| `repz ret`, `jmp fword ptr` and `fld tbyte` each ended the whole run with a traceback | the first three attempts at a full lift |
| The host executable's own image base is `0x00400000` — exactly where the game wants to be | the first attempt to run it |
| Moving the host is not enough: the loader fills that range before any user code can reserve it | the second attempt |
| An import's identity is (DLL, name), not the name — eOkaoDt and eOkaoGn both import `ordinal_302` and they are different functions | the runtime reported 27 board imports where there are 40 |
| The code range came from `.text`'s VirtualSize, but the loader maps the larger of VirtualSize and SizeOfRawData — and `mainCRTStartup` is 0x36 bytes past VirtualSize, in the raw tail | the full image died on its very first dispatch |
| 2,763 "functions" were addresses inside instructions; `0x0081C100` is the third byte of `fld dword ptr [0x8E0848]` and lifted to `hlt` | the boot ran an instruction that is not in the binary |
| Dropping one of those leaves the neighbour clamped onto an address nothing lifts | the next run stopped on an unresolved dispatch |
| A function cut at a shared epilogue has branch targets with no body — 2,586 of them | ditto, one function later |
| SEH is validated against the TEB's stack bounds, and lifted code never runs on that stack | `OutputDebugStringA` ended the process with no message at all |
| hybrid's 32 KB emulated frame is not a stack for a worker thread running the game's call graph | a guard page nobody could grow |

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
