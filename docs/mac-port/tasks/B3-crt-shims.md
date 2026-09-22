# B3 — CRT and string shims

- **Milestone:** M1
- **Depends on:** A1
- **Blocks:** B6
- **Status:** in progress — mac-port-B3
- **Size:** `stricmp` 71 files, `_snprintf` 12, `_vsnprintf` 8, `_stricmp` 5, `_access` 4,
  `_mkdir` 1, `__int64` 22

## Why

The Microsoft CRT spellings. Individually trivial, collectively the difference between a build that
starts and one that does not.

## Scope

`GameEngine`, `GameEngineDevice`, `Main`, `Libraries/Source/WWVegas`. Note the counts are files,
not call sites — `stricmp` in 71 files is the bulk of this task.

## Do

1. One header of shims, next to B2's. Map the Microsoft spellings to POSIX:
   `stricmp`/`_stricmp` → `strcasecmp`, `strnicmp` → `strncasecmp`, `_snprintf` → `snprintf`,
   `_vsnprintf` → `vsnprintf`, `_access` → `access`, `_mkdir` → `mkdir` (note the mode argument),
   `__int64` → `int64_t`.
2. **`_snprintf` and `snprintf` are not the same function.** MSVC's returns negative and does not
   null-terminate on truncation; C99's returns the length it wanted and always terminates. Any
   caller that checks the return value needs reading, not replacing. There are only 12 files; read
   all of them.
3. `__int64` → `int64_t` is safe and mechanical. Do it properly rather than `#define`-ing it, and
   include `<cstdint>` where it lands.
4. Path separators: 331 string literals in `GameEngine` and `GameEngineDevice` contain a
   backslash. macOS tolerates neither in a path. Most of these are `.big` archive-internal paths,
   where the backslash is part of the on-disk format and **must not change**. Only the ones that
   reach the real filesystem need normalising. Do not sweep this blindly — C1 owns filesystem path
   handling, so find them, list them on this task, and leave the filesystem-bound ones for C1.

## Done when

The shim header builds on both; no Microsoft-only CRT spelling remains outside it and the Windows
platform layer; Windows full build and `ctest` green.

Every `_snprintf` caller whose return value is used is named in the pull request description with a
line saying why the new behaviour is correct there.

## Do not

- Do not touch archive-internal path strings. A `.big` file's table of contents uses backslashes
  and always will.
- Do not add a compatibility `#define stricmp strcasecmp` and call it done. Rename the call sites;
  the macro will collide with something in six months.
