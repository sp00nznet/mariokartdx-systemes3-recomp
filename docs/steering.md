# Steering, and the four things in the way of it

Measured against Mario Kart Arcade GP DX v1.00.32 with an Xbox pad. Three of
the four are fixed; the fourth is unmeasured because it needs a controller
that is awake, and the pad slept through six test runs.

## The path, in order

```
pad  ->  DirectInput device  ->  rec+0x47C (DIJOYSTATE2)
     ->  rec+0x62C (the wheel, normalised)  ->  0x0063D000  ->  the kart
```

### 1. The axis enumeration callback was never reachable — fixed

Finding a device is one enumeration; finding its axes is another.
`0x007401AF` calls `EnumObjects` through the device vtable's slot 4 and hands
`DINPUT8.dll` a guest address, `0x0073FF80`, which was never planted and never
appeared among the `[r2l]` fallback redirects either. The real DLL called an
address that is not code and the axis walk found nothing.

Planted, and confirmed running: five callbacks, `dwOfs` 0/4/8/12/16, types
`0x...02` — X, Y, Z, Rx, Ry of a gamepad.

### 2. The axis range was a rounding error — fixed

`0x00740879` reads the raw axis, subtracts a centre and divides by the double
at `0x008E0C30`, which is **1000000000.0**; the clamp above it compares against
the same 1e9. So the wheel this game expects reports an encoder spanning
±1e9. A gamepad reports 0..65535, which is **0.000033** of that - measured, the
game's wheel value moved 0.0000 to 0.0001 across the pad's entire travel.

`SetProperty(DIPROP_RANGE)` over the whole device, unacquiring around the call,
makes dinput8 do the scaling and leaves the game's arithmetic untouched.
Accepted with `hr 00000000`, and the raw axis then spans the full ±1e9 with the
wheel value tracking it exactly.

### 3. The steering mode fell through to a constant — fixed

```
0063D0A7  cmp dword ptr [eax + 0x77c], 2   ; the steering mode
0063D0AE  je  0x63d0cb                     ; 2: keep the analog wheel
0063D0B0  fld dword ptr [0x86aa8c]         ; anything else: 0.3
```

`0.3` of full lock, constantly, in one direction. That single constant is both
the kart steering itself into the wall and the menus cycling one way.

The mode comes from `0x0073FF90`, a three entry table at `0x008DA25C` matching
the device's product name and returning the mode beside it, or 3 for no match:

| | mode | name |
| --- | --- | --- |
| [0] | 0 | `Controller (XBOX 360 For Windows)` |
| [1] | 1 | `JC-PS101U` |
| [2] | 2 | `Thrustmaster T500 RS Racing wheel` |

Only a T500 gets the analog path. The name this runtime writes, `Immersion
TouchSense Steering Wheel (USB HID)`, is in none of them. That name is not
wrong either - a different check at `0x007403FE` wants exactly it, and matching
there is what designates the device as the wheel at all. One device cannot
carry two names, so the mode is answered at the lookup (`ES3_WHEEL_MODE`).

Modes 0 and 1 are not alternatives: the consumer tests for 2 specifically, so
they fall to the same `0.3`.

### 4. The gate above it — not yet measured

```
0063D071  mov eax, [ebx + 0x0c]            ; ebx is the SINGLETON, not the manager
0063D074  cmp dword ptr [eax + 8], 0       ; device count
0063D07A  mov ecx, [eax]                   ; the record designated the wheel
0063D07C  cmp dword ptr [ecx + 0x590], 0   ; its state
0063D083  je  skip                         ; skipped: the wheel is never loaded
0063D087  fld dword ptr [ecx + 0x62c]      ; the wheel
```

With mode 2 the constant pull stops but the stick is inert, which is what a
skipped gate looks like: the mode-2 branch keeps whatever is already on the FPU
stack. `mgr+0` and `mgr+0x10` are set by `0x0074044B` only when the Immersion
name matches, and both are settled at startup - so `ES3_TRACE_INPUT` now reports
them from attract mode, no race required.

## Pointer chains, which cost three wrong tracers

The game's own code at `0x0063C649` is the authority:

```
mov esi, [0x00959B54]      ; the singleton
mov ecx, [esi + 0x0c]      ; the manager
cmp [ecx + 8], edx         ; device count
mov ecx, [ecx]             ; the wheel record
```

So the manager is **singleton + 0x0C**. A tracer that read `ebx` at the
consumer's entry segfaulted the game; guarded, it reported a manager of
`004C0045` - wide character text - with 410880264 devices. Committed memory is
not meaningful memory, and `+4` borrowed from the input tracer's different
chain was no better. Take the base the game uses.

## The test hazard

The game enumerates controllers **once, at startup**. A pad asleep at that
instant leaves the whole run with no input, buttons included, and it looks
exactly like a regression in whatever was last changed - it produced one report
that a change had broken all input when that change had never executed. The
runtime now prints `[in] THE GAME FOUND NO CONTROLLER.` rather than leaving it
silent.
