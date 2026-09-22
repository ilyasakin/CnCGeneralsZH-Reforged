# Handoff — wwlib, and the B3 shim behind it

Written 2026-09-22 by the agent that did B3 (CRT and string shims), W1 (wwlib), and A1 (the CMake
split) before them. Written for a stranger.

## 1. State

**Branch `feature/mac-port-wwlib`, head `24426364`.** It is **12 commits behind
`feature/mac-port`** (integration head `cd576471` at the time of writing) — rebase before doing
anything else.

Three commits were made here. Two are already on the integration branch by content, under different
hashes because the PM rebased them:

| Commit | Content | On integration? |
|:--|:--|:--|
| `f37615a7` | `bool.h`, `Point.h` | yes |
| `88fd7f35` | `CRC.H`/`crc.cpp`, `random.cpp`, `int.cpp`, `lcw.cpp`, `multilist.h`, `srandom.cpp`, `strtok_r` | yes |
| `24426364` | the Win32-only wide-string guard, `cpudetect.h`'s `sint64`, `strcmpi`, `ffactory.cpp`, `rawfile.cpp`, the `WCHAR` note in `MSVCCompat.h` | **no — only here** |

So one commit is unmerged. Check with content, not `git merge-base --is-ancestor`: the hashes differ
after the rebase, and that check gave me a nonsensical answer (a child merged, its parent not).

**`wwlib` is 58 of 82 sources compiling** on this branch, measured with
`Tools/syntax_sweep.py` (added in this handoff commit). It does **not** link and no `wwlib`-dependent
test runs yet. `detround_selfcheck` passes. `compression` is 28 of 28 and
`compression_selfcheck` passes all six codecs **only with B5's one-line `<windef.h>` guard on
`BaseType.h`** applied; without it, 26 of 28.

## 2. Uncommitted

**Nothing.** `git status` is clean. Keep nothing, throw nothing away.

Two things in the worktree are untracked *and needed*, and a fresh worktree will not have them:
`Libraries/Source/Compression/ZLib` (27 files) and `LZHCompress/CompLibHeader` + `CompLibSource`.
They are gitignored vendored sources. Get them with A2's `vendor.sh`, or copy them from another
worktree. Without them `cmake` configure fails on the vendored-sources check.

## 3. The next step, concretely

Rebase, then run:

```console
cmake -S GeneralsMD/Code -B build-mac -DCMAKE_BUILD_TYPE=Release
cd GeneralsMD/Code && python3 Tools/syntax_sweep.py ../../build-mac wwlib --files
```

**Of the 24 failures, 21 are B5's and you should not touch them**: `windows.h` ×7, `ddraw.h` ×3,
`LPCSTR` ×2, `HWND` ×2, and one each of `HANDLE`, `HMODULE`, `HINSTANCE`, `DWORD`,
`OutputDebugString`, `INVALID_HANDLE_VALUE`, `oaidl.h`. The three that are not:

1. **`mixfile.cpp:321`** — `_MAX_DRIVE` and then `_splitpath`. `_splitpath` parses drive letters, so
   it is a filesystem-path question and belongs with **C1**, not a shim. Adding the `_MAX_*`
   constants alone unblocks nothing, because `_splitpath` is the next error.
2. **`mpu.cpp`** — `<intrin.h>` for x86 `__cpuid`/`__rdtsc`. Needs an arm64 answer or a Windows
   guard. **B14/B5.**
3. **`registry.cpp`** — Win32 registry throughout. It should probably be Windows-only outright
   rather than shimmed, which is a decision rather than a fix.

Three of B5's 21 are `ddraw.h` (DirectDraw, via `dsurface.h` and `misc.h`), which is renderer
surface rather than a type shim; the PM has asked B5's owner to rule on whether those go to the
D-track.

## 4. What I know that is not written down

- **Filenames in WWLib are mixed case and macOS hides it.** The real names are `CRC.H`, `Point.h`,
  `INI.H`, `TARGA.CPP`, `WATCOM.H`. A case-insensitive volume lets you open `crc.h` and edit the
  right file, and git records it under the tracked name — but `git show branch:.../crc.h` **fails
  silently**, which made my first merge-state check report four changes as absent that were present.
  If you scan the tree against a branch, use the names `git ls-tree` prints.
- **Why the wide half is guarded rather than deleted.** Guarding is reversible in one line; deleting
  `Get_Wide_String` is a judgement about whether anyone ever wants it back, and nobody here is
  positioned to make it. Agreed with B1's owner.
- **`WW3D2`'s `WCHAR` is deliberately untouched** and should stay that way until B1 reaches it.
  `render2dsentence.h`, `render2d.h` and `font3d.h` carry a `WCHAR` interface the engine reaches at
  five sites in `W3DDisplayString.cpp` and `W3DGameWindow.cpp`. Guarding them would hide five real
  conversions. The only WW3D2 line I touched is `texfcach.cpp:700`, a one-word `strcmpi` rename.
- **Things I looked at and set aside:** `chunkio.h:256,316` use `(const WCHAR *)` inside macros —
  harmless, because the macros are not expanded anywhere `wwlib` compiles. `mutex.h:184` mentions
  `WideStringClass` in a comment only. The `CRC` class (as opposed to `CRCEngine`) uses
  `unsigned long _Table[256]`, which is 8 bytes per entry on macOS — **values are unaffected**
  because every operation masks to `0xFF` or `0x00FFFFFF`, so I left it. If someone pins it anyway,
  that reasoning is why it was not urgent.
- **What I was about to check and did not:** whether `obscure.cpp`'s three `CRCEngine` call sites
  produce values that are stored anywhere persistent. If they are, the `CRC.H` width fix changes a
  stored value on macOS relative to an older macOS build — irrelevant to Windows parity, but it
  would matter to anyone who had already generated data with the broken width. Nobody has, so it is
  theoretical, but I did not confirm it.
- **A failed approach worth not repeating:** my first instinct on `crc.h` was to add `_lrotl` to the
  shim. That would have compiled and produced a **different CRC**, because the accumulator and the
  staging buffer were both `long`. Shimming the function without pinning the widths is the wrong fix
  and it is silent.
- **`-fsyntax-only` is not a build.** 58 of 82 passing the front end does not mean 58 objects link.
  Nothing has linked `wwlib` yet and there may be a tail of link errors nobody has seen — missing
  symbols from the guarded-out wide half are the obvious candidate.

## 5. Open questions, and who owns them

| Question | Owner | Where it landed |
|:--|:--|:--|
| Does `WCHAR` survive as a name? | B1 (agent 3a) | **Resolved: no.** 3a answered that it should not become a compatibility alias, and the reason is that the only implementations behind it are Win32 APIs. The "deliberately absent" note is in `MSVCCompat.h` in commit `24426364`, in 3a's own wording. |
| Do `ddraw.h`'s three files belong to B5 or the D-track? | B5 (agent 83) | Open; PM has asked for a ruling. |
| `_splitpath` / drive letters | C1 | Open, not yet raised with C1's owner. |
| `mpu.cpp`'s x86 intrinsics | B14 / B5 | Open. |

## 6. The one thing a Windows build could falsify

`CRC.H` and `crc.cpp` pin `CRCEngine`'s accumulator to `int32_t`, its staging buffer to
`char[sizeof(int32_t)]`, and replace `_lrotl(CRC,1)` with an explicit 32-bit rotate.

**The whole change rests on one premise: `long` is 32 bits on Windows**, so all of that is the same
type and the same arithmetic it already was. If the premise is wrong, every CRC this engine produces
changes, and `obscure.cpp`, `ini.cpp`, `TagBlock`, `crcpipe`, `crcstraw` and `miscutil` all consume
it. It is the `high` row in `WINDOWS-DEBT.md` and it is the first thing to check when a Windows
machine exists.

**The check already exists**: `Tests/test_wwlib.cpp`'s `crcengine_accumulator_is_32_bits_wide`
asserts `0x93b0f838` for `"Westwood Studios"`. Run it on Windows. It should pass unchanged; if it
does, the premise held. Do **not** rely on `crcengine_byte_at_a_time_matches_block` — that test
passes at either width, because both of its sides move together, which is why the bug survived
thirty years of it.

## 7. `Tools/syntax_sweep.py`

Every number in this document came from it. It reads the source list and flags out of the build tree
so it cannot disagree with CMake, compiles each source alone, and attributes each failure to the
line that says `error:` — **not** to a filename grepped from the output, because clang prints an
`In file included from` chain first and grepping it attributes every failure to the wrong header.
That mistake produced three contradictory tables for `wwmath` in one afternoon. The comment at the
top of the file says so; please do not simplify the regex.
