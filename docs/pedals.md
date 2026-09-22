# The pedals

Steering works. The accelerator and brake do not, and this is what is known
about why — written down because four separate theories were tried and
measured dead in one session, and none of them should be tried again.

## What works, and is proven

The input side is solved end to end.

An XInput pad reports both triggers separately. **DirectInput does not** — it
puts them on one axis, `lZ`, centred at 32767, with `lY` flat at zero. So by
the time DirectInput hands the state over, the two pedals are a single number
and nothing downstream can separate them again.

`GetDeviceState` is the last place they are still two things, and the runtime
now intercepts it (`mk_get_device_state` in `host.c`), putting the two XInput
triggers back on the two axes the game expects. Measured with both pulled:

```
pedals in: gas=1.000 brake=0.000 -> lY=65535 lZ=0
pedals in: gas=0.000 brake=1.000 -> lY=0     lZ=65535
pedals in: gas=1.000 brake=1.000 -> lY=65535 lZ=65535
```

Full travel, both, independently, into a 272-byte `DIJOYSTATE2`. The pad is
not in question: a standalone XInput probe shows `LT` and `RT` both reaching
255.

**How to hook it matters.** Swapping the device object's vtable pointer for a
patched copy kills the device outright — no steering, no buttons, only the
JVS coin still arriving — because dinput8 evidently identifies its objects
through that pointer. Patching the single slot in place, under
`VirtualProtect`, is fine. That mistake cost a build.

## What is still wrong

The kart does not accelerate. The value arrives and something ignores it.

Note the game's own trivia card: auto-accel engages when the accelerator is
**not pressed within a second of the start**. So auto-accel appearing is a
*symptom* of the pedal not arriving, not a setting suppressing it — the
operator menu is not the answer, and `TEST` not working is a separate bug.

## Four theories, each measured dead

**1. The scale.** 1000 and 65535 behave identically. Not a range problem.

**2. The per-axis calibration block.** `0x00740240` zeroes four runs of eight
floats from `+0x5AC` in steps of `0x20`, and it is still all zeros at run
time:

```
axis 0 calib: 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0
axis 1 calib: 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0
axis 2 calib: 0.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0
```

Dead because **axis 0 is the wheel**, whose calibration is equally zero and
which works perfectly. The wheel takes the separate `÷1e9` path at
`0x00740879` instead.

**3. The global bound at `0x00952D10`.** The normalisation at `0x00740A67`
compares against `fldz` and this global — not the calibration block. The
global is in `.bss`, is zero for the whole run, and **nothing in the program
writes it**: three reads, no writes. Dead for the same reason as (2) — the
wheel goes through this block too and is fine.

**4. `[[0x00959B54]+0x10]+0xAC/+0xB0`.** `0x0063C8E0` — the function that
applies the steering — fills the kart's input struct from these two, which
looked exactly like throttle and brake sitting beside the wheel. Traced:

```
kart pedals: +0xAC=0.0000 +0xB0=5.3509
kart pedals: +0xAC=0.0000 +0xB0=47.2396
```

`+0xB0` climbs steadily and `+0xAC` never moves: that is **speed**, feeding
the force feedback the drive board wants. Useful to know, not the pedals.

## Where to look next

The throttle's consumer has not been found. The wheel's was
(`0x0063D000`, reading `record+0x62C`, called from `0x0063C8E0`), and finding
it is what fixed the steering — the value had been correct for days while the
consumer took a different source entirely.

Two specific leads:

- **Nothing outside the input module reads `record+0x630` or `+0x634`.** The
  wheel's `+0x62C` has a real consumer; the pedal fields appear to have none.
  Either they are copied into another structure inside the module, or the
  throttle genuinely comes from somewhere else.
- **The serial port the game drives is not JVS.** Packets are
  `FF FF FF 01 ...` with no `E0` sync — that is the drive board. The JVS
  emulation on COM1 may be answering a device the game is not asking.
  `JVSEmuMK.dll` ships with the game and is loaded by the patched entry stub
  rather than imported, so it is a candidate for where the cabinet's switches
  and pedals really arrive.

The method that worked on the steering is the one to use: find what consumes
the throttle, and print the value **beside the branches that decide its
fate**, not on its own. A value that is correct and discarded looks identical
to one that is working.
