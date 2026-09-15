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

### Following E05-55 back, with a debug register

`ES3_WATCH_MEM` was written for this and answered it in one run. Point it at
the error word - it takes the same chain syntax as `ES3_PEEK`, which matters
because the word is behind two pointers into the heap and is somewhere else
every run:

    ES3_WATCH_MEM="959b64**+3c"
    [watchmem] 0A32993C written at host 21A56373, in lifted 005C37E0

and `dispatch_owner()` names the lifted function. From there `ES3_WATCH_VA`,
which now sees direct calls, walked up the chain one run at a time:

| | |
|---|---|
| `0x005C37E0` | `AddError(code)` - scans the five slots at `[obj+0x3C]` and appends if the code is new |
| `0x005C2C50` | the generic raise-an-error wrapper; the code arrives as its argument |
| `0x005BF4D0` | **the decision.** `if (0x00679470() && [[0x00959B5C]+0xCDC] == 0) raise(0x38)` |
| `0x00679470` | true when `[0x0095A894]`, the All.Net client object, is null - or when `[0x0095A850]+0x94`, its status, is non-zero |
| `0x00679530` | creates that object, and runs only if `[0x00952914]` is set when `0x00678410` reaches `0x006785B6` |

So the panel is not about a server refusing to answer. It is about the client
object never being constructed - and about `[[0x00959B5C]+0xCDC]`, which is
the other way out of the `if` and reads non-zero exactly in the runs that
reach the operator menu instead. That is a race, not a configuration: two
runs with identical settings land differently.

### Open: text rows drawn on top of each other

On the operator menu and the error panel, some lines land on top of each
other, glyph by glyph - `REMAINING TE` over `REMAINING SERVICES:`,
`<OFFLINE OPERATION>` over `PLEASE WAIT.`, `TIME (UTC):` over
`MK3100-1-NA-MPRO-A32 (Rev.1.00.32)`, and on the error panel `LOCAL NETWORK
ERROR` over `ERROR AUTH NG`.

What is NOT wrong: the glyphs inside each string are spaced correctly, and
the menu list itself stacks correctly. So the font metrics are fine and the
row pitch is fine; it is the origin handed to some rows that repeats the
previous one.

It is not the operand-order trap that `fxch` was, either. Capstone reports
`fst`, `fstp`, `fld`, `fadd` and `faddp` with a single operand - only `fxch`
and the `fcmov`s carry the implicit `st(0)`, and this image contains no
`fcmov` at all. Checked, so the next person does not check it again.

### A correction, and a second gate

The frame loop's test is written up above as `0x005C38B0` returning true
meaning "skip the task tick". The polarity is the other way round. The code is

    006AB9D2  call 0x5c38b0
    006AB9D7  cmp  al, 1
    006AB9D9  je   0x6aba86      ; 1 SKIPS the tick; 0 falls through to it

and `0x005C38B0` returns 1 only when some slot holds a mode whose flag in the
table at `0x00871A10` is 1. With every slot at `0x66` it returns 0, which is
the running case.

And it is not the only gate. Immediately after it,

    006AB9E2  call 0x6ffcc0
    006AB9E7  cmp  byte [ebx + 5], 0
    006AB9EB  jne  0x6aba12      ; jumps PAST 0x00746B90

where `ebx` is the frame loop's argument - the task, from `[ebp+8]`, and the
caller at `0x006AA78D` checks `[eax+4]` the same way to skip the frame whole.
So `+4` and `+5` are that task's own "do not run me" flags, and they are set
while a system screen is up. Measured with every error slot at `0x66`:
`0x005C38B0` is called 84 times in one trail window and `0x00746B90` not once.

Which means forcing the tick is the wrong move: gameplay is paused on purpose
because the game is in test mode, and the way to attract mode is to leave that
screen.

### The other half: Namco's own API, which speaks JSON

The dead host is two services. All.Net is the `naominet.jp` half, with its
`key=value` PowerOn. `amk3-stg.nbgi-amnet.jp` is Namco's own, and it is not
All.Net at all - it is a JSON API, and it is the one this boot actually
reaches. The paths are a UTF-16 block at `0x004766F0`:

    /board/getControlData      /amid/getAmid           /amid/checkAmid
    /amid/checkAlive           /amid/unLock            /amid/getAccessCode
    /amid/getProvisionalAmid   /amid/updateProvisionalAmid
    /board/saveFaceRecognition /incoming/save

Answering `/0.01/board/getControlData` with an All.Net reply gets rejected in
the game's own words, with our body quoted back:

    *INF* [stat=1&uri=http://127.0.0.1/&...] このJsonの解析はフォーマットが違う

"this Json's format is wrong" - which is how the format was identified. The
fields are not guessed either: `store_id`, `allnet_game_id`,
`allnet_game_ver`, `line_type`, `store_name`, `store_nickname`, `area_cd_0`,
`area_name_0..3`, `country_code`, `time_zone`, `status`, `started_at`,
`yuai_option_limit_at` are a block in `.rdata` at `0x00487BF0` - a
getControlData response written out. Answer with those and the game says

    *INF* ErrorCode:0

Getting that far needs the boot past its own network test first, which is two
pre-hooks in `games/mariokartdx/src/host.c` - `0x005BF340` and `0x00679470`.
They have to be hooks and not `ES3_POKE`: the test at `0x005BF516` runs once,
a few seconds in, and `[this+0x60] = 3` is never undone, so a value held down
at ten hertz arrives after the decision every time.

### Exit code 6 was a stack overflow, and nothing could say so

Past the network test the boot logged `*INF* ErrorCode:0` and the process
ended with code **6**, having logged nothing else.

Every door was watched and every door stayed shut. `exit`, `_exit`, `abort`
and `TerminateProcess` are imported by the game and bound to `hle_give_up`,
which prints; none fired. `kernel32!ExitProcess` and `kernel32!TerminateProcess`
were patched with five bytes of `jmp`; neither fired. `ntdll!NtTerminateProcess`
was patched with a real trampoline - the last door, the one the other two end
through - and it did not fire either. The TLS callback at `DLL_PROCESS_DETACH`
did not run. There was no Windows Error Reporting record, which normally means
another process did the killing; nothing was.

`py -3.11 -m tools watch` answers it in one run. It launches the game with
`DEBUG_ONLY_THIS_PROCESS` and reads the kernel's own account:

    EXCEPTION C00000FD STACK OVERFLOW at 0x234c5cc8  thread 66164  first chance
    EXCEPTION C0000005 ACCESS VIOLATION at 0x7754f637 thread 66164  SECOND CHANCE

A thread ran out of stack, and the fault while dispatching *that* fault is one
the kernel does not try to deliver: it ends the process where it stands,
before any user-mode handler, vectored or otherwise. That is why four hooked
exit paths all stayed silent. `SetThreadStackGuarantee(64 KB)` now runs on
every thread that enters lifted code, which does not prevent an overflow but
leaves the kernel room to report one.

The floor moved from 8 MB to 16, and the numbers are measured, not chosen:

| stack | what happens |
|---|---|
| 8 MB | overflows eight seconds in; process gone, log stops mid-line |
| 16 MB | boots |
| 20-32 MB | thirty worker threads reserve too much; the guest's own loads start failing and the run dies at a sound file, which looks nothing like a stack |

The last row is the real constraint: the thread stacks and hybrid's callback
arenas spend the same address space. Halving the arena buys the stacks their
room, so all three are knobs now - `ES3_THREAD_STACK_MB`, `ES3_HYBRID_ARENA_MB`,
`ES3_HYBRID_FRAME_MB`. `ES3_HYBRID_ARENA_MB=16 ES3_THREAD_STACK_MB=32` runs.

The same run also showed 176,727 first-chance access violations in nine
seconds, 86,754 of them at one address. Those are not a bug: guest code is
mapped without execute, so a callback that reached a real library unthunked
faults at the address that was called and the handler dispatches the lifted
version. It is the mechanism working. It is also most of the run's time.

### The resolver was answering with the one address the client refuses

With a stack it could live on, the boot got to the PCB startup checklist -
drive unit, I/O, NAMCAM, steering, IC card reader, local network,
authentication, update - and to `NOW LOADING`. And the ALL.Net client still
never connected. It resolved `naominet.jp`, then `tenporouter.loc`, and
stopped: no socket, no request, panel reading `LOCAL NETWORK ERROR` /
`ERROR AUTH NG`.

`alAbEx` validates an address before it will use one, in six instructions at
`0x007B5EA0`: `ntohl`, reject `<= 0.255.255.255`, reject `127.0.0.0/8`, reject
`240.0.0.0` and up. Loopback - the only address this runtime handed out - is
the one answer it is certain is wrong. `alAbExInit` returns non-zero at
`0x00464334`, the client's status word at `[this+0x94]` goes to 4, and
`0x00679470` reports the network as a problem for the rest of the run.

So the two resolvers now answer differently, because they have different jobs:

| | |
|---|---|
| `gethostbyname` | `192.0.2.1` - TEST-NET-1, RFC 5737, reserved for documentation and guaranteed not to be a real host. `connect()` and `sendto()` put it back on `127.0.0.1`, where the listener is. Nothing opens a port the LAN can reach. |
| `getaddrinfo` | `127.0.0.1` - it feeds the client's traceroute, which is a raw ICMP echo, and on a machine with no store router only loopback answers one |

`getaddrinfo` was not hooked at all until this session, which is why
`tenporouter.loc` had been timing out: the name is not resolved with
`gethostbyname`, and `ERROR DNS TIMEOUT` / `ERROR TIP HOST NOTFOUND` was the
panel saying so.

Measured after, where before there had never been a request at all:

    [game] *INF* Traceroute to 127.0.0.1, 10 hops max.
    [game] *INF*   1 hops to destination address.
    [allnet] connect -> 127.0.0.1:80
    POST /sys/servlet/PowerOn HTTP/1.0
    [allnet] /sys/servlet/PowerOn -> stat=1&uri=http://192.0.2.1/&host=192.0.2.1

and `[[0x0095A850]+0x94]` reads 0 - the client is up - with the object holding
the `http://192.0.2.1/` it was given.

The two pre-hooks that used to fake a working network (`mk_net_ok`,
`mk_boot_net_state`) are off by default now, behind `ES3_FAKE_NET_OK`. They
were scaffolding for a dead resolver, and with the resolver working they are
worse than nothing: `mk_boot_net_state` writes into `[0x0095A850]+0x90`, which
is the live client object, and with it on the client never posts `PowerOn` at
all.

### Every cabinet check, and what each one costs

E08-01 was the lesson. The camera failing its boot check is not a line on a
panel: `0x005C38B0` returns true when any of the five error slots holds a mode
whose twelve-byte entry at `0x00871A10` begins with 1, and the frame loop skips
the **whole task tick** when it does. Modes 56, 66, 70, 81, 82 and 96 are all
such entries. So any one of these errors is the game building no scene at all,
for ever - which is what "attract mode is not rendering" had been the whole
time.

| error | what it is | answered by |
|---|---|---|
| **0x51/0x52** | no Namco I/O board on the USB bus | `mk_io_board_count`, and the twelve-digit serial in the same breath |
| **E05-55** (56) | the cabinet is not authenticated | the dongle record, written at the gate as well as at the check |
| **E07-11** (66) | the IC card reader answered nonsense | `jvs.c` was claiming every COM port; it now claims one |
| **E08-01** (70) | NAMCAM, the camera | `[this+0xAE0]` zero, which is the game's own "not fitted" |
| **E22-12** (96) | the STR PCB, over a serial cable | `[[0x00959B38]+0x180]+0x49`, the flag the driver would set |

Two of those deserve their own note.

**The dongle.** `0x005C23C0` opens `F:/dongle.bin` with `fopen("rb")` and copies
eight bytes over `[this+0xCE0]`, which the constructor copies to `[this+0xCDC]`.
There is no F: drive here, and the game already knows what to do about that -
`0x005C254D` sets the byte to 1 when the dongle cannot be read. It just does not
survive: `0x005C1ED0` runs later, finds the byte zero and writes zero over both.
So the stand-in goes in as a pre-hook on that function - and in two places,
because `[[0x00959B5C]]` and the `0x005C1ED0` object are not always the same
one. The board handler next door has been printing `<- they differ` all along.

**The card reader.** This one was self-inflicted. `es3_jvs_open()` answered any
COM port, and Mario Kart opens COM1 for the JVS I/O and COM2 or COM4 for the
card reader (`0x005BD830` picks by name). Answering the card reader in JVS is
worse than not answering it: the port opens, the game talks, and it gets replies
that mean nothing - `0x005BD8CF` compares the result against **-301**, finds it,
and raises E07-11. Five and a half thousand times in one run.

### The serial boards speak first, which is why jvs.c has nothing to say

`ES3_TRACE_JVS` on this game shows the same two lines for ever:

    [jvs] SetCommTimeouts: interval 600, read 2 x n + 600
    [jvs] read posted: 3 byte(s) into 09252632, ovl 09249530, routine 00745F40

A three-byte overlapped read with a completion routine, posted again and again,
and **not one byte ever written**. The board is expected to talk first. jvs.c is
a JVS board that answers requests, so it waits, and the game waits, and the
eighty-second timeout does the rest. Standing in for the one flag each board
sets is what gets past it; making the runtime initiate is the real fix and is
not done.

### The chain from "no attract mode" to one null pointer

The boot finishes and the screen is the operator menu with `<OFFLINE OPERATION>`
over `PLEASE WAIT`. That is not the boot waiting; it is the boot having decided
it cannot run networked. Tracing back from it, every link is now known:

| | |
|---|---|
| `0x005BF50A` | `[task+0x60] = 3`, the offline state. Nothing moves the task out of it |
| `0x005BF4D0` | reached only when `[[0x0095A850]] != 0`, the session's "initialised" byte |
| `0x005BF516` | needs `[[0x0095A850]+0x90] == 0x67`. It reads **0** |
| `0x00464400` | is what writes that word, from `0x007B3770` - the alAbEx poll. 0x65 is in progress, 0x67 is done, 0x69 is an error. Zero means **the poll never ran** |
| `0x00463890` | is the poll's caller, and it returns immediately unless `[[0x00959B1C]+3]` and `[[0x0095A850]]` are both set |
| `0x004636A0` | sets `[[0x0095A850]] = 1` at `0x0046376D`, on a straight line with no failure path - so if it runs to the end, the session is initialised |
| `0x005BF2C7` | is the gate above all of it: `[[0x00959B1C]+3]` zero skips the whole network step, which is why nothing in this runtime was ever asked about the network. `ES3_POKE=959b1c*+3=1` sets it |
| `0x00678D30` | then decides, and it is two tests: `[0x00952827] != 0` (it is 1) **and `[0x0095A87C] != 0`** |

`[0x0095A87C]` reads **0**, every run. It is the cabinet-link object - the LAN
between the cabinets of one bank, not All.Net - and it is never constructed.
An earlier session had already found its other end: `[[0x95A87C]+4]+0x1C`
staying negative is what the frame tick reports as `LOCAL NETWORK ERROR`.

So the last thing in the way is a second cabinet. The link worker at
`0x006770B0` opens a broadcast socket, sends an eight-byte `MK3` discovery
packet and waits for a peer; with the interface fix it hears its own packet
back, which is enough to get an address adopted and not enough to build the
object. Either the runtime stands in for a peer on the wire, or the object has
to be built with one node in it.

Two things measured on the way that are worth not re-measuring:

**The cabinet-boot path reaches NOW LOADING.** With `959b1c*+3=1` the game gets
past the operator menu into the real game load - frame 400 is `NOW LOADING`,
and a surviving run is at frame 1500 and beyond. It also dies with code 6 about
one run in three, partway through that load, reporting nothing. `tools watch`
cannot catch it: under a debugger the run never gets that far.

**The death is not the reply.** Answering `getControlData` with `{"status":0}`
made one run survive where the full reply had died, which looked decisive and
was not. Four runs each way: the empty reply died two times in four, the full
reply once in four. Four fields in and four fields out land on either side of
it at random, which is what bisecting a coin flip looks like.
`ES3_ALLNET_FIELDS=<n>` is still there for when there is something real to
bisect.

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
