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
| Renders | **A swap chain, a frame loop and ~20 presents a second - and an empty scene.** Direct3D 10 and a 1360x768 DXGI swap chain are created and `IDXGISwapChain::Present` is called about twenty times a second. The back buffer is read out before each present (`ES3_SHOT=`) and every frame is **1,044,480 of 1,044,480 pixels black**: no `Clear`, no `Draw`, no Direct3D 11 call at all between one present and the next. The frame itself is real - 690 guest calls across 143 functions - so the renderer is running over an empty draw list |
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

### It renders

```
[shot] frame 2500 -> es3_frame_2500.bmp  1360x768, 229140 of 1044480 pixels are not black (21%)
```

The recompiled game draws. Read out of the swap chain's own back buffer, not
photographed off a window: a rounded red panel with a gradient and a white
border, the game's outlined display font, **"Please call an attendant."** in
Mario Kart yellow, error lines beneath it, `CREDIT(S) 00 / 02` in one corner
and `MK3 Rev.1.00.32` in the other. Earlier in the boot it draws its
**"NOW LOADING"** spinner. Every shader, the font, the 2D pipeline, the
Direct3D 10 device and the swap chain: working.

Getting there meant answering the cabinet, one piece at a time, each found by
following the frame loop's own refusal to tick its tasks:

| what the game asked | where | answered with |
|---|---|---|
| how many I/O boards are on the USB bus | `0x007A8590` | one (`es3_bind_guest`) |
| a twelve-character cabinet ID | `[obj+0x498]`, checked at `0x005C1F8D` | `271000020001` - the format is fixed: `2710`, then the variant, then `02`, then the unit |
| the coin mechanism's state | `[0x009595C0]` | 2, "running" (`ES3_POKE`) |
| the I/O board's name | `[[0x9599F8]+0x2E8]`, `strcmp`d against `"NA-JV"` | `NA-JV` |

Each one, unanswered, parks a slot at `[[0x959B64]]+0x3C` in a mode whose flag
in the table at `0x00871A10` is 1; `0x005C38B0` sees that and the frame skips
`0x00746B90` entirely, so nothing updates, nothing is built and the back
buffer is presented untouched. That is the whole black screen, and the modes
walk 0x51 -> 0x52 -> 0x0D -> 0x0E -> 0x46 as each answer lets the next
question be asked.

### What is left: All.Net

The last one is not a cabinet part, it is a dead service. The game reported

> ERROR DNS TIMEOUT / TIP HOST NOT FOUND
> GAME CANNOT START UNTIL A NETWORK CONNECTION IS MADE

Behind that name is Sega's All.Net client - `alAbEx Ver 2.00.07`, statically
linked, rolling its own HTTP over raw WS2_32 - posting to
`http://naominet.jp/sys/servlet/PowerOn` and reading back `stat`, `uri`,
`host`, `name`, `nickname`, `region0`, `region_name0..3`, `place_id`,
`country`, `timezone` and a clock. Namco's own `amk3-stg.nbgi-amnet.jp` is the
second half, over WINHTTP.

`allnet.c` answers it where it asks: `gethostbyname()` returns 127.0.0.1 and a
listener on 127.0.0.1:80 replies to `/sys/servlet/PowerOn`. No hosts file, no
proxy, nothing off the loopback adapter. `ES3_NO_ALLNET` turns it off,
`ES3_ALLNET_PORT` moves it, `ES3_TRACE_NET` prints every request.

Two things about it are not the obvious thing, both measured. The FIRST name
the game resolves is its own hostname, from `gethostname()` at `0x006781BF`,
to learn the cabinet's IP - answering 127.0.0.1 to that turned `DNS TIMEOUT`
into `LOCAL NETWORK ERROR`, because the game concluded the cabinet was on
loopback. And the reply drains the request body before writing, because
closing a socket with unread data sends an RST and an RST discards the reply.

The DNS timeout is gone. What the screen says now is

> LOCAL NETWORK ERROR / ERROR AUTH NG
> NBLINE POINTS ARE AT 0 / PLEASE CHARGE

and the next measurement is the surprising one: **the All.Net client is never
asked**. `0x007B5470`, which picks between the PowerOn and DownloadOrder URLs,
does not run through frame 8000. No `connect()`, no `WinHttpConnect`, no
socket of any kind - with all of WS2_32 traced.

The local network check itself passes. `WSAStartup` succeeds at `0x00678A59`,
`SIO_GET_INTERFACE_LIST` at `0x00678382` finds a non-loopback interface and
leaves 172.19.0.1 in `[0x00952924]`, and the network init at `0x00678410`
returns 1 on its second call - the first returns 0 and `[0x0095291C]` counts
0xB4 frames down to the retry.

So the question is no longer what a server should answer. It is which gate
keeps the network manager from asking, and the candidate is `[0x0095A87C]` -
the manager object, allocated by `0x00675F20`, and every network branch in the
per-frame tick at `0x00678B20` is `cmp dword [0x95a87c], 0 / je`.

### The error panel is a list, and it is readable

The panel is not one error. `0x0071FDFD` walks five slots and prints the
first, reading each as `[[[0x00959B64]] + 0x3C + 4*i]` and indexing two tables
- `0x00932080` for the text, `0x00931EE8` for the code on the label. Read at
runtime with

    ES3_PEEK="959b64**+3c"

the five come back as `38 66 66 66 66`: one error, `0x66` being the "nothing
here" sentinel. 56 is **E05-55**, Shift-JIS for "network connection
incomplete" - so `LOCAL NETWORK ERROR`, `ERROR AUTH NG` and `NBLINE POINTS ARE
AT 0` are three lines of detail under a single condition, not three
conditions.

Blanked with `ES3_POKE="959b64**+3c=66"`, the next one surfaces: 70, **E08-01**,
"NamCam (camera) error" - the OKAO Vision camera board, which this machine
does not have either. Blank all five and the game leaves the panel and draws
its **operator test menu** - MENU (DRIVE UNIT), GAME OPTIONS, I/O TEST,
MONITOR TEST, SOUND TEST, NETWORK TEST, `MK3100-1-NA-MPRO-A32 (Rev.1.00.32)`,
and `<OFFLINE OPERATION>` along the bottom.

That is a poke, not a fix - but it says the gates are enumerable, that each
one is a named condition with an address, and that there is a real interactive
screen behind them.

And then, with the interface fix in and **nothing poked at all**, the same
screen arrives on its own: the game leaves the error panel and draws the
operator test menu. The runs that still showed the panel had `ES3_TRACE_NET`
set, and printing every UDP packet slowed the link worker enough to lose its
own race - the observer, not the observed. Turn the tracing off and the boot
gets there.

E05-55 comes from `L_006791C0`, which asks of each of five slots: is this slot
me - `[0x009253EC] == slot` - and if not, is it a connected peer? A cabinet
that has adopted a virtual adapter's address is neither.

Ruled out by measurement on the way, so nobody repeats them:

| | |
|---|---|
| the display | D3D9 sees 0 adapters here and DXGI 6 adapters with 0 outputs, because this is a remote session - but windowed D3D10 works anyway, and `dxgi_output.c` hands DXUT the output it wanted |
| DXUT's remote-session refusal | answered; `GetSystemMetrics(SM_REMOTESESSION)` returns 0 |
| a modal dialog nobody clicks | printed and answered OK - that is how "Could not find any compatible Direct3D devices" was read at all |
| occlusion | tested before and after the task tick started; raising the window changes nothing |
| waiting longer | frame 8000, twenty minutes, working set plateaued |
| the JVS serial board | `jvs.c` answers the protocol; the game's driver is receive-first and never transmits, so the serial link is not the I/O path that matters |
| `JVSEmuMK.dll` | loads, and patches code in memory - which a static recompilation never executes. `es3_guest_diff()` confirms it rewrote no guest code |
| running the wrong build | the tree ships two executables 33 bytes apart, differing at the entry point; `guest_load()` now checks the fingerprint |
| reading a stale swap chain | the game makes exactly one, and `es3_dxgi_present()` tracks the latest anyway |

### The x87 bug that cost a round

The frame loop above used to never finish a frame, and the reason is worth
recording because nothing about it looked like a floating-point problem.

The thread carrying the game's whole call graph was pinned inside one lifted
function, dispatching nothing - invisible in the trail, which only records
calls. That function is the fixed-timestep accumulator: add the delta, then
subtract the step until what is left drops below a threshold. `fxch` was lifted
as a swap of `st(0)` with itself, so it subtracted from the wrong register, the
step came out negative, the accumulator grew instead of draining, and the loop
never exited. The game booted, opened everything, loaded its data, and ticked
one frame for ever.

Capstone reports `fxch st(1)` with **both** registers, `st(0)` first, so reading
the first operand always gives `st(0)`. 24,268 of them in this image. Next to
it, `faddp st(1)` is `st(1) += st(0)` and was being lifted as `st(0) += st(1)`,
which writes the result into the slot the following `fpop` discards - another
~80,000 sites. Both were already fixed once in pcrecomp's other lifter and had
come back in this one. Fixed upstream, with a `--selftest` that reads the
emitted C so they cannot come back a third time.

What is left is speed:

| | |
|---|---|
| dispatches per second | **2.4 million**, about **400 ns** each |
| guest calls per frame | about **35 million** - it is still loading |
| hottest function | `0041EAA0`, called 97,000 times in one sampled window |

`0041EAA0` is `fabsf`. Two instructions. Every one of those calls went through
the ring buffer, a watch check, an import-range check, a thunk check, a table
lookup and an indirect call into a C function that sets up a CPU frame.

**A direct call should be a direct call.** The lifter emits
`dispatch(c, 0x0041EAA0u)` for `call 0x41eaa0`, and the driver knows from its
own output that `0041EAA0` is one of the functions it lifted - so it can emit
`L_0041EAA0(c)` instead and skip the lookup entirely. That is the next piece of
work and it is in [systemes3recomp's README](systemes3recomp/README.md) in
full, along with two things that were measured and did not help.

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
