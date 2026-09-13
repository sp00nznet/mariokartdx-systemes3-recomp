# The builds, measured

*Mario Kart Arcade GP DX* was revised for seven years, and the revisions are
not cosmetic: the import table, the section layout and the code size all move.
This is what is actually in each executable we have, read out of the binaries
rather than off a version screen.

```
$ py -3.11 -m tools pe MK_AGP3_FINAL_v1.00.32.exe
```

| | **v1.00.32** | **v1.18.16** | **v1.06.35 "OF"** |
|---|---|---|---|
| Built | 2013-04-23 | 2020-11-16 | 2022-08-01 |
| File size | 5,816,832 | 7,432,704 | 11,599,872 |
| `.text` | 4,308,348 | 5,513,100 | 8,234,716 |
| Entry | `0x007CB996` | `0x008C6E3A` | `0x00B88768` |
| Sections | 5 | 5 | **7** |
| Imports | 495 / 27 DLLs | 513 / 31 DLLs | 531 / 28 DLLs |
| Purge derivable | **489** | **509** | **526** |
| PDB path | `D:\work\MK3\repos\branches\Master_1st\` | `F:\workspace\rom\branches\jpn\update8\` | `G:\global_rom\branches\global\Update3_BNA1Lite\` |

All three: PE32, i386, image base `0x00400000`, MSVC linker 10.00, Windows GUI
subsystem, no `DYNAMIC_BASE`.

## Which one to work on

**v1.00.32.** It is the smallest by a wide margin — 4.3 MB of code against 8.2
— it is the original 2013 Japanese release, and it is the only one of the three
that is unmodified.

## v1.06.35 is not a Namco build

Worth stating plainly, because the version number makes it look like the oldest
and the file is the largest. Its section table gives it away:

```
.text  .rdata  .data  .llvm_ad  .rsrc  .reloc  .mAGo
```

`.llvm_ad` is an LLVM address-significance table, which a 2010 MSVC toolchain
does not emit — so parts of this binary have been through a second toolchain
since Namco shipped it. `.mAGo` is a 4 KB section appended after `.reloc`,
which is where a patcher puts its payload. It also imports `JVSEmuMK.dll!engate`
directly, where the untouched builds reach the I/O board through
`DeviceIoControl`.

None of that makes it useless — it is the only build in the set whose JVS path
is visible in the import table, which is genuinely informative about what the
I/O layer has to provide. It makes it a bad *reference*. Measure against
v1.00.32 and read v1.06.35 for hints.

## What changed between 1.00 and 1.18

The import table is the most legible diff, and it is all cabinet:

| added by v1.18.16 | what |
|---|---|
| `bngrw.dll` (11) | the Bandai Namco card reader |
| `Nbam_QR_Code.dll` (6) | the QR code on the player's card |
| `PSAPI.DLL` (2), `OLEAUT32.dll` (+2) | housekeeping |
| `IPHLPAPI` 9 → 11 | more network probing |
| `MSVCR100` 180 → 188 | a larger C++ surface |

The card reader and the QR encoder arrive together, which dates the hardware
revision they belong to. `SETUPAPI` is present in both and gone from
v1.06.35 — device enumeration for the I/O board.

## Other ES3 titles

The toolkit is title-agnostic: everything it knows about a game it learns from
that game's executable. *Mario Kart Arcade GP DX* is the first target because
it is the one we have, and because its four revisions make a good control — a
change that works on one build and not another is a change that was fitted to a
binary rather than to the platform.

## What is not System ES3

`game.iso` in the parent directory of this tree is **Mario Kart Arcade GP**
(2005), header `GGPE01` — a GameCube-derived Triforce disc, not an ES3 title
and not in scope here. Same series, different machine entirely. *Mario Kart
Arcade GP 2* (2007) is Triforce too. The DX in this repo's name is load-bearing.
