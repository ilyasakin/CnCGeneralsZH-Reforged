# Handoff index — 2026-09-22

Six agents worked this plan for one session. Each wrote its own record; this is the index and the
state of the tree they left.

## Measured state, not reported state

Everything here was rebuilt and re-run on the integration branch before being written down.

| | |
|:--|:--|
| Branch | `feature/mac-port`, **131 commits** ahead of `main` |
| `cmake` configure on arm64 | clean |
| `ctest` | **93% passing**, 1 failing, 5 registered `DISABLED` |
| `wwlib` front end | 24 → **58 of 82** |
| `wwmath` front end | **32 of 36** |
| `compression` | builds; `compression_selfcheck` passes all six codecs |
| M1 tasks | 6 → **19** |
| `WINDOWS-DEBT.md` | **75 rows** |

**`-fsyntax-only` is not a build.** The `wwlib` and `wwmath` numbers are front-end numbers.
Nothing has ever *linked* `wwlib`, and there may be a tail of link errors nobody has seen. Use
`Tools/syntax_sweep.py` and read its caveat.

## The records

| Agent | Record | Carried |
|:--|:--|:--|
| `-95` | [`95-wwlib.md`](95-wwlib.md) | B3, `wwlib`, `DetRound`, `crc.h`, `Tools/syntax_sweep.py` |
| `-83` | [`83-b5.md`](83-b5.md) | B5, B8, B11, B13, the threading sweep |
| `-8d` | [`8d-d1-b2-b14.md`](8d-d1-b2-b14.md) | D1 recon + PR1, B2, B14, the vendored sweep |
| `-21` | [`21-e3-and-sweeps.md`](21-e3-and-sweeps.md) | E3, zutil, persistfactory, GameSpy |
| `-14` | [`14-b16-and-b6.md`](14-b16-and-b6.md) | B4 recon, B7, B10, test audit, B6 prep |
| `-3a` | [`3a-b1.md`](3a-b1.md) | B1, B15, the `%ls` sweeps |

## Start here

1. **B16 — `wwdebug`'s `windows.h`.** Unowned, unstarted, and it blocks **six** tests including
   `wwmath_selfcheck`, which `CMakeLists.txt` singles out as "the first evidence that determinism
   survives clang on arm64". Its task file's site table was wrong until today: the two errors you
   hit first are `FormatMessage` at `:69` and `GetLastError` at `:82`, not the three it listed.
2. **B5 — the Windows API surface in `wwlib`.** One task from `wwlib` building. **Re-measure
   before scheduling**; two agents got materially different counts on different trees and the
   handoff gives the command rather than the number.
3. **B1's typedef flip.** Everything before it is done; it never got the go-ahead because the gate
   is `gameengine` compiling, which is behind B16. Its last open question was answered before its
   author stopped, and the answer is **"don't"**: `Lib/BaseType.h` is not reachable from WW3D2
   today, and pulling it in would bring `NULL` redefined against four WWLib headers that already
   define it as `0L`/`(0L)` — ill-formed. The three WW3D2 headers need exactly one thing, the
   `WideChar` typedef, not three hundred lines of engine preamble. Give it its own small header.

## What to distrust

Four rules came out of this session, each from a measured failure, and they are in the plan's rules
section. In order of how much time they will save:

1. **Say what your check cannot see.** Five checks agreed with the code under test while both
   differed from the truth, because each shared the property it was testing.
2. **Estimates did not survive measurement, three times, always by the same mechanism.** A fatal
   `#include` error stops the translation unit, so the next problem is invisible until the first is
   fixed. Measure *after* every fix, never before.
3. **Never define `_UNIX`.** Sixty sites of an abandoned 1990s port. Two have been read and both
   were holes rather than implementations; fifty-eight have not.
4. **Grep the vendored sources for platform predicates** — and know the three ways that check reads
   nothing.

## Tooling traps, all measured on this machine

- `grep` is `ugrep` and honours `.gitignore`. From the repo root it skips **466 tracked files**,
  including 54 in two **built** libraries. Every vendored directory also carries a committed
  `.gitignore` containing `*`, so `grep -rn socket Libraries/Source/GameSpy` returns **0** where
  `/usr/bin/grep` returns **854**.
- Attribute a compiler error to the **error line**, not by grepping output for filenames — that
  matches the `In file included from` chain and produces a confident, wrong table.
- **WWLib's filenames are mixed case** (`CRC.H`, `Point.h`, `INI.H`, `TARGA.CPP`). A
  case-insensitive volume hides it, but `git show branch:path/crc.h` **fails silently and returns
  nothing**, which reads exactly like "content absent".
- `zsh` does not word-split unquoted variables, which silently corrupted two sweeps.

## Still true and still open

The plan's six defects in the shipping Windows game, `WINDOWS-DEBT.md`'s 75 rows, and E1's standing
item: **the determinism question is half answered.** E3 covers the architecture axis — all 96
`DetTrig` rows identical across arm64 and x86_64, including an 80,000-value fingerprint. It says
nothing about MSVC, and its header says so in as many words. Do not let a green E3 be read as a
green Windows.
