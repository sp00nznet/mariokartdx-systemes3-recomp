# Roadmap

Where this goes now that it plays. Ordered roughly by how much each one is
worth against how much is already known, not by how much fun it is to write.

Everything below is a *host* feature. None of it touches the lifted game code,
which is the point: the recompiled executable is a native Win32 program, so
the things a native program can have, it can have.

**Baseline today.** Boots, renders at 1360x768, plays on a gamepad — coin,
character, track, steering, accelerator, brake, items, a finished race —
banapassport minting and unlocks, and **sound working well**. Measured gaps:
21-23 fps against a 60 fps cabinet, and an in-race camera that sometimes
hangs.

---

## 1. A real host window

A Win32 shell around the swap chain: menu bar, **File / State / Controls /
Video / Debug / About**, with proper glyphs rather than text-only menus — the
look OpenNote has.

This is the one that unlocks most of the rest, because every other item below
needs somewhere to live that is not an environment variable. Right now the
runtime has something like forty `ES3_*` switches and every one of them is a
thing a person has to know before launching.

Known: the game asks for an exclusive fullscreen swap chain at 1360x768 and
the runtime already intercepts that and makes it windowed (`ES3_FULLSCREEN`).
So the host already owns the window and the swap chain — the menu bar is a
matter of owning the frame around it rather than wrestling anything away from
the game.

Constraint worth writing down: **windowed stays the default and fullscreen
stays opt-in.** A recompiled arcade game that grabs the display on launch is
unusable on a working machine.

## 2. Save states

Save the whole machine mid-race and come back to it.

This is the most valuable item and the most work, so it is worth being honest
about the shape of it.

**The easy half.** The guest is a flat 32-bit address space plus a `CPU`
struct per thread. Both are ours. Writing them to a file is close to trivial,
and for a single-threaded program it would be the whole job.

**The hard half.** This game is not single-threaded and does not live only in
guest memory:

- around thirty lifted threads, each with a host stack and a hybrid arena,
  and a snapshot has to catch them all at a point where none is mid-call
  into a real DLL;
- Direct3D and DXGI objects, which are host-side and cannot be serialised —
  they would have to be torn down and rebuilt from a recorded description;
- open handles: the serial ports, DirectInput devices, sockets, files;
- the card reader, which has its own state machine.

The tractable version is a **safe-point snapshot**: pick a moment the frame
loop already passes through — the top of the task tick is the obvious one —
quiesce the other threads there, snapshot guest memory and every `CPU`, and
record a description of the host objects rather than the objects. Restoring
rebuilds D3D from that description and reopens the rest.

That is a real project, but nothing about it is blocked on unknowns.

## 3. User-editable controls

A binding UI, and a config file behind it, replacing the compiled-in map in
`jvs.c` and the `ES3_*` switches around it.

Cheap, and it should come with the window. The current map is already
documented in one table in `jvs.c`; this is turning that table into data.

Worth including: a per-axis calibration and dead zone, which would have saved
this port a lot of grief — see [pedals.md](pedals.md) for how long it took to
discover that the pedals are not axes at all.

## 4. Video modes

- fullscreen on/off, exclusive or borderless;
- **render-to-4K, or any multiple** — the game draws at 1360x768 and a blit
  at Present time can scale that to whatever the monitor is. Integer scaling
  and a choice of filter, because a 2013 arcade title upscaled with bilinear
  looks worse than one upscaled sharply;
- **downscale / reduced internal resolution** for weaker machines. This
  matters more than it sounds: the port runs at 21-23 fps against a 60 fps
  cabinet, and that is the single biggest thing standing between "it plays"
  and "it plays properly";
- a frame-rate readout, so the previous point can be measured rather than
  felt.

The swap chain is already the runtime's — `[dxgi] swap chain at ...` — so
this is a question of what the host does between the game's Present and the
screen.

## 5. Multiplayer — four linked cabinets

The game banks **up to four cabinets on a LAN**, and the mechanism is already
understood because the runtime had to defeat it to boot:

```
00676A20  counts the peers: walks four slots at [this+0x12194] and counts
          those whose [+0xC] and [+0xD] are both set
0067618C  call 0x676a20
00676191  cmp eax, 1              ; exactly one: get on with it
00676197  jne 0x6761a2            ; otherwise wait, and time out at 60
```

`mk_cabinet_link` currently answers **1** — a standalone cabinet that counts
itself. Multiplayer is the same slots filled in for real: four peers, each
one another instance of this port, with the game's own link traffic carried
between them.

The route is a **lobby server the port checks into when the player enables
it** — matchmaking and NAT traversal outside the game, then the game's own
protocol over the established link. There is an existing private lobby server
that can serve as the model for the protocol and the check-in flow; the
design should not assume it, and a self-hosted server has to be a first-class
option.

Unknowns to settle before committing: what the peer slots actually carry,
whether the link is peer-to-peer or through a designated cabinet, and how
tolerant the game is of latency it never saw on a LAN. All answerable by
reading `0x00676A20`'s neighbours.

## 6. A debug menu over the operator settings

Every switch in the cabinet's own system menu, as checkboxes — **auto-accel
first**, since it engages whenever the accelerator is late and there is
currently no way to turn it off.

Known: these live in a settings blob at `[[[0x00959B38]+0x188]+0x34]`, one
byte each — bytes `0x19`..`0x1C` are the device-fitted flags, and the rest of
the block is the same shape. The operator menu itself is reachable in
principle (`TEST`), but the keyboard binding for it only works when the game
window has focus and the pad binding does not appear to register — so a host
menu that edits the bytes directly is both easier and more useful than fixing
the in-game route.

This also subsumes several existing `ES3_*` switches: free credits, the
camera, the drive board, the steering source.

## 7. USB camera passthrough

The cabinet photographs the player and puts their face on the kart. The
runtime currently declines to have a camera, which is the honest answer on a
desk and the reason the NAMCAM checklist row reads OFF.

The game reaches it through OMRON's OKAO Vision (`eOkaoDt`, `eOkaoPt`,
`eOkaoAg`, `eOkaoCo`, `eOkaoGn`) — face detection, facial parts, age and
gender estimation — all by ordinal, and those DLLs ship with the game, so the
real libraries can answer. What is missing is frames: a real USB camera
through DirectShow, handed to the capture path the probe at `0x0073ECF0`
looks for.

Lowest value of anything here and explicitly nice-to-have, but it is the most
*characteristic* thing about this cabinet, and the hardware is a webcam.

---

## Not on this list, and why

**Rewriting the renderer.** The game's Direct3D 9 and 10 work. The frame rate
problem is in the lifted CPU code, not the graphics, and the fix for it is
profiling, not a new backend.

**Emulating the board.** The whole premise is that this is a Win32 program on
a PC. Every piece of cabinet hardware is answered where the game asks for it,
and that stays true.
