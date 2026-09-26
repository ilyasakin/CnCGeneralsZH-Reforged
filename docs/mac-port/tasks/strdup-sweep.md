# strdup sweep (defect #31), 2026-09-26 (-18)

## Why

On Windows, `strdup` is the UCRT's `_strdup`, which returns NULL for a NULL argument. That is -18's reading of the UCRT, not measured by this project. Darwin's and glibc's `strdup` read through the pointer instead.

A cloned particle emitter's NULL user string crashed the fog of war off Windows (README, defect #31).

## What changed

Every call in the engine's own code whose argument is not a literal now goes through `strdupAsWindows` (`Libraries/Include/Platform/StrdupAsWindows.h`):
- **Windows:** it calls `_strdup` itself, so nothing changes there.
- **Elsewhere:** NULL gives NULL, and anything else is `strdup`'s copy.

The helper is applied even where the argument is provably non-NULL. There it is identical, and it keeps one spelling tree-wide.

## Checked

The census found 42 matches in GameEngine, GameEngineDevice, `Libraries/Source/WWVegas`, `Libraries/Include` and `Main`:
- 39 are replaced calls;
- 1 is a literal, kept;
- 2 were the fix's own interim helper in `part_emt.cpp` (its definition and its comment line), now removed in favour of the shared one.

The two copy-constructor sites that crashed were already on that interim helper, and are replaced too.

**Can be NULL: the helper is the fix.**

| site | argument |
|---|---|
| `WW3D2/part_emt.cpp` copy constructor (two) | `src.NameString`, `src.UserString`: the user string is NULL until set. **Defect #31.** |
| `WW3D2/part_emt.cpp:873` `Set_Name` | a caller's `pname` |
| `WW3D2/part_ldr.cpp:293` `Set_User_String` | a caller's `pstring` |
| `WW3D2/part_ldr.cpp:306` `Set_Name` | a caller's `pname` |
| `WW3D2/agg_def.h:113` `Set_Name` | a caller's `pname` |
| `WW3D2/hlod.cpp:381, 384, 410` | `Get_Name()` of a LOD, a hierarchy tree and a model |
| `WWSaveLoad/parameter.h:229` | a caller's `new_name` |
| `WWLib/argv.cpp:409, 415` `Update_Value` | a caller's `value` (not checked there, unlike `:442`/`:447`) |
| `WW3D2/font3d.cpp:62`, `WW3D2/texfcach.cpp:709` | a caller's file name |
| `WWAudio/WWAudio.h:512` `operator=` | `src.string_id` |
| `WWAudio/WWAudio.cpp:1578`, `WWAudio/SoundBuffer.cpp:157` | a caller's name. Neither file is built anywhere (no CMake target), but `WWAudio.h` is included by two built WW3D2 files. |

**Provably non-NULL: identical either way.**

| site | why |
|---|---|
| `GameNetwork/ConnectionManager.cpp:2066` | a local buffer, after the early return above it |
| `GameNetwork/GameInfo.cpp:1135, 1281` | `AsciiString::str()`, which returns a static empty string for an empty string, never NULL |
| `GameSpy/Thread/PersistentStorageThread.cpp:630, 998, 1024` | `std::string::c_str()` |
| `WWLib/argv.cpp:217` | `ptr`, dereferenced by the token loop before it |
| `WWLib/argv.cpp:278` | `string`, after `strlen(string)` |
| `WWLib/argv.cpp:442, 447` | guarded by `if (attrib)` and `if (value)` |
| `WWLib/ini.cpp:503, 554` | the line buffer, and `divider` after `strlen(divider)` or its `" "` replacement |
| `WWLib/ini.cpp:1639, 1667` | guarded by `== NULL` returns and `string != NULL` |
| `WWLib/ini.cpp:1812` | guarded by `if (defvalue == NULL) return NULL` |
| `WWLib/ini.cpp:1844, 1879, 1890` | `entryptr->Value`, built only from checked strings |
| `WWLib/rcfile.cpp:79` | guarded by `if (filename)`. Built on Windows only. |
| `WW3D2/agg_def.cpp:613`, `WW3D2/hlod.cpp:662, 663, 832`, `WW3D2/part_ldr.cpp:553` | a fixed-size name array read from a W3D chunk |
| `WW3D2/part_emt.cpp:106` | the literal `"ParticleEmitter"` (kept as `::strdup`) |

## The wider net: what else differs for a NULL argument

- **An invalid-parameter handler.** The engine installs none (no `_set_invalid_parameter_handler` anywhere). So on Windows the UCRT's parameter-validating functions (`_stricmp`, `_strlwr`, `fopen` and others) terminate on NULL, which is no more tolerant than a crash off Windows.
- **Non-validating string functions** (`strlen`, `strcmp`, `strcpy`, `strcat`) read through NULL on every platform alike.
- **`printf`'s `%s` with NULL** prints `(null)` in the UCRT, Darwin and glibc alike.
- **`_wcsdup`/`wcsdup`:** no uses in the tree.
- **Callers of the same pointers:** `strcmp`/`stricmp` fed by a name that can be NULL fail on Windows as well, so they are not port differences. `_strdup` is the only NULL-tolerant call found.

## Not checked

- Third-party libraries.
- `Tools/` (WorldBuilder and the other editors).
- Whether any code relies on a NULL *result* from the Windows `strdup` in some way other than storing it.
- The UCRT itself: no Windows machine was used.
