cmake_minimum_required(VERSION 3.20)

project(GeneralsMD CXX C)

# Which operating system this build is for.  MSVC used to answer that question too, because for
# twenty years there was only one answer, and this file used the two interchangeably from its first
# line to its last.  They are different questions now.  Everything that is really about the
# platform - the CRT, the Windows SDK import libraries, MIDL, DirectX, the window - asks
# ZH_PLATFORM_WINDOWS; only a flag written in one compiler's own syntax asks which compiler is
# reading it.  docs/mac-port/README.md has the rest of the plan.
# Windows against POSIX, not Windows against macOS.  The port targets macOS first but is meant to
# reach Linux and other POSIX systems, so "is this not Windows" is ZH_PLATFORM_POSIX, and the two
# refinements below are for the few things that genuinely differ between POSIX systems (a window,
# audio, CPU detection) - never for "is this not Windows".  An unported platform-specific file fails
# with an #error naming what it lacks (cpudetect.cpp does this), which is louder and more useful than
# refusing to configure.
if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
  set(ZH_PLATFORM_WINDOWS TRUE)
else()
  set(ZH_PLATFORM_POSIX TRUE)
  if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    set(ZH_PLATFORM_MACOS TRUE)
  elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(ZH_PLATFORM_LINUX TRUE)
  endif()
endif()

# The other half of the split.  A target that only Windows can build is gated out below; a target
# whose sources do not compile under clang *yet* is a different thing, and this is it.  Those stay
# defined on macOS, so that tasks B1-B6 have something to drive with --target and see real errors
# from, but out of the default build, so that a Mac build failure means a regression in what was
# working rather than the next task's work showing through.  Take a name off the list as its task
# lands.  On Windows this expands to no argument at all and changes no target.
if(ZH_PLATFORM_POSIX)
  set(ZH_NOT_YET_PORTABLE EXCLUDE_FROM_ALL)
else()
  set(ZH_NOT_YET_PORTABLE "")
endif()

# compile_commands.json: every source's exact compile command, which Tools/syntax_sweep.py and
# clangd read.  Ninja can produce it on demand; a Makefiles tree has nothing else that carries each
# target's own -D flags.  POSIX only because only the POSIX tooling reads it - include_case_check
# among them - and it changes no target and no command line.
if(ZH_PLATFORM_POSIX)
  set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif()

# The zip goes to machines that have never installed a Visual Studio redistributable, and
# generals.exe imported MSVCP140.dll and VCRUNTIME140.dll by name.  That fails in the Windows
# loader before a line of our code runs, so there is no DebugLogFile.txt and no crash file to
# send back - the same undiagnosable failure the retail Miles and Bink DLLs are packaged to
# avoid.  Link the CRT into the exe and into d3d8.dll instead of depending on a machine having
# it.  CMAKE_POLICY_DEFAULT is what reaches the vendored subdirectories: d3d8to9 declares
# cmake_minimum_required(3.12), which predates CMP0091, and would otherwise ignore the setting
# below and keep importing the DLL.
# Both of these are Windows concerns and both are read later than this - CMAKE_MSVC_RUNTIME_LIBRARY
# when a target is created, CMAKE_POLICY_DEFAULT_CMP0091 when a vendored subdirectory is added - so
# the guard costs the Windows build nothing.
if(ZH_PLATFORM_WINDOWS)
  set(CMAKE_POLICY_DEFAULT_CMP0091 NEW)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# x64 only.  The 32-bit build was dropped once this one was proven against it: same simulation to
# the bit, same picture to within sub-pixel sampling, and sound and video of its own that do not
# depend on the two 32-bit DLLs the game shipped with.  Nothing here is written for x86 any more.
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "Configure for x64: cmake -A x64")
endif()

set(WW ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/WWVegas)
set(COMP ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/Compression)

# Libraries/Include holds the handful of headers that belong to no library and are wanted by all
# of them: Lib/BaseType.h, Lib/Trig.h and, since B2, Lib/Clock.h.  Thirteen targets read the clock
# - the five WWVegas libraries, the four dx11 ones and three of their tests - and naming each of
# them here is a list that goes stale the first time somebody adds a target and forgets.
#
# This is a directory-wide include path and nothing else.  The warning further down about not
# defining things globally is about compile *definitions*, where a mismatch between libraries is a
# link error or a differently-shaped struct; an extra /I carries no such risk, and the three
# headers behind it are included by name and by nobody who does not want them.
include_directories(${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

# The third-party sources EA stripped out, which this repository does not carry either.  build.bat
# fetches them before it configures; say so here rather than let the generator report a missing
# source file per library.
set(VENDORED
    ${COMP}/ZLib/deflate.c
    ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
    # litehtml and nanosvg are common rather than Windows-only: gameengine links litehtml and
    # compiles nanosvg into HtmlOverlay.cpp, and gameengine is what the macOS build is for.
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/litehtml/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/nanosvg/nanosvgrast.h
    # The GameSpy SDK is built everywhere since B6 (see "gamespy" below).
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/GameSpy/CMakeLists.txt)
# The DirectX 8 headers are only read by targets this file does not define on macOS, and A2 decides
# what a Mac fetch pulls down.  Demanding them here would stop a Mac configure over files nothing
# portable includes.
if(ZH_PLATFORM_WINDOWS)
  list(APPEND VENDORED
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/DirectX/Include/d3d8.h)
  set(VENDOR_HINT "Run Tools\\vendor.ps1 (build.bat does it for you).")
else()
  # SDL3 and miniaudio are the platform layer everywhere but Windows (decision 3).
  list(APPEND VENDORED
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/SDL3/CMakeLists.txt
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source/miniaudio/miniaudio.c)
  set(VENDOR_HINT "Run Tools/vendor.sh (build.sh does it for you).")
endif()
foreach(vendored ${VENDORED})
  if(NOT EXISTS ${vendored})
    message(FATAL_ERROR "${vendored} is missing. ${VENDOR_HINT}")
  endif()
endforeach()

# The _CRT_ pair only tells MSVC's own headers to stop deprecating the C library this code is
# written against.  They mean nothing to any other compiler and cost nothing there, so they stay
# unconditional.  WIN32 and _WINDOWS are statements about the operating system, and most of the
# tree reads them as exactly that, so they move into the Windows branch: defining them on a Mac
# would tell several hundred #ifdefs the wrong thing.  A few places read WIN32 to mean "not
# 16-bit" instead; those are source fixes for tracks B and C, not a reason to define it here.
add_compile_definitions(_CRT_SECURE_NO_WARNINGS _CRT_NONSTDC_NO_WARNINGS)
if(ZH_PLATFORM_WINDOWS)
  add_compile_definitions(WIN32 _WINDOWS)
endif()

# EA's own switch for exactly this situation ("Good for startup problems", Debug.h):
# it turns DEBUG_LOG back on in a Release build so the hundreds of existing log
# calls in GameEngine::init land in Run/generals.exe-<machine>.log.  Without it a
# quiet failure in init leaves nothing behind at all - GameEngine::init's
# catch (ErrorCode) swallows everything that is not ERROR_INVALID_D3D, and WinMain's
# outer catch (...) swallows the rest, so the exe just returns 0.  Applied per target
# at the bottom of this file, not globally: WWVegas' wwdebug.h includes GameEngine's
# Debug.h and routes WWDEBUG_SAY into DEBUG_LOG, so a global define makes wwmath and
# friends call DebugLog(), which only exists in gameengine - every leaf test exe then
# fails to link.  The WW libraries' own chatter is not what we are after anyway.
option(RELEASE_DEBUG_LOGGING "Write the startup debug log in Release builds" ON)
if(MSVC)
  # /permissive- is off on purpose: this is 2003-era code and templates here rely on
  # pre-two-phase-lookup name binding (simplevec.h et al).
  # /EHa, not /EHsc: WinMain installs _set_se_translator(DumpExceptionInfo) to turn an
  # access violation into a C++ exception and write ReleaseCrashInfo.txt on the way.  A
  # translator only runs in code compiled with asynchronous EH, so under /EHsc the hook
  # is dead (MSVC says so: "warning C4535: _set_se_translator() requires /EHa") and a
  # faulting frame just kills the process with 0xC0000005 and no log at all.
  add_compile_options(/W3 /permissive /EHa)
  # There is no debugger on the porting machine, so ReleaseCrashInfo.txt's stack
  # dump is the only stack we get.  StackDump.cpp walks the EBP chain by hand and
  # /O2 implies /Oy (no frame pointer), which leaves that dump empty; /Zi + /DEBUG
  # then give the frames names instead of bare addresses.  Costs a PDB, buys Phase 5.
  # /FS goes with /Zi: MSBuild compiles a project's sources in parallel and they all write
  # the one PDB.  Without it the writes go through mspdbsrv.exe, and if that dies or is
  # killed - which happens when a build is interrupted - every following compile fails with
  # C1041, or worse, only some of them do and the library is left half-built against an old
  # header.  /FS serializes the writes in the compilers themselves instead.
  add_compile_options(/Oy- /Zi /FS)
  # /MP: build a project's own sources in parallel too, not just one project at a
  # time.  MSBuild already runs projects concurrently, but gameengine alone is ~580
  # sources in a single project and was the long pole.  /FS above is what makes this
  # safe - the PDB writes are serialized in the compilers.
  add_compile_options(/MP)
  # There is no /arch:SSE2 here and none is needed: x64 has no x87 code generation at all, so
  # every float is computed at its declared width and every machine gets the same bits.  That is
  # what the 32-bit build had to ask for explicitly, and why it pinned the x87 precision control.
  # See dettrig.h and FPUControl.h.
  add_link_options(/DEBUG)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  # Keyed on the COMPILER, not the OS.  These flags are what a GCC or Clang build needs to compute
  # the simulation the way MSVC does, wherever it runs.  It used to read elseif(ZH_PLATFORM_MACOS), so
  # a Linux build would have configured cleanly with none of them - and GCC's default is
  # -ffp-contract=fast, and plain char is UNSIGNED on aarch64 Linux.  Both silent desyncs.
  # The clang half of the same decisions.  None of the switches above transfers: /EHa is there for
  # _set_se_translator, which is a Windows structured-exception mechanism; /Oy- and /Zi are there
  # for StackDump.cpp's hand-written EBP walk, which no arm64 build has ever had; /FS and /MP are
  # about one MSBuild project writing one PDB.  Those are Windows reasons and they do not cross.
  # What is left is the short list of places where clang's defaults differ from cl.exe's in a way
  # this code notices.
  #
  # -fexceptions: the engine throws from the bottom and catches at the top - GameEngine::init's
  # catch (ErrorCode) and WinMain's outer catch (...) are structural, not decoration.  clang turns
  # them on for C++ by itself; saying so keeps a -fno-exceptions in some future preset from
  # deleting the error handling without a diagnostic.
  add_compile_options(-fexceptions)
  # -ffp-contract=off: the most important line in this file, and not optional.  clang on arm64
  # contracts a*b+c into one fused multiply-add, which rounds once where two operations round
  # twice.  MSVC on x64 never does it.  The simulation is full of multiply-adds, so a single
  # contracted instruction inside it makes this build compute different numbers from the Windows
  # build - and that surfaces as a replay that diverges and a network game that desyncs, not as a
  # compile error.  dettrig.h went to integer arithmetic over a committed table for exactly this
  # reason; this is the other half of the same promise, and E1 is the test that holds it.
  add_compile_options(-ffp-contract=off)
  # -fno-strict-aliasing: 2003 C++.  The .big and .w3d readers cast a byte buffer to a struct
  # pointer and read through it, which the standard does not allow and an optimiser is entitled to
  # act on.  MSVC does no type-based alias analysis, so this code has never been compiled by
  # anything that would.
  add_compile_options(-fno-strict-aliasing)
  # -fsigned-char: MSVC's plain char is signed and code here compares one against zero.  Darwin on
  # arm64 already signs it, unlike the ARM EABI, so this pins a default rather than changing one -
  # which is the point, because the default is the kind of thing that is cheap to state and
  # expensive to discover.
  add_compile_options(-fsigned-char)
  # -g for the reason /Zi is there: there is no debugger on the porting machine, so a symbolised
  # stack is the only stack anyone gets.  Deliberately no -fno-omit-frame-pointer - that would be
  # propping up StackDump.cpp's frame walk, and C5 replaces that mechanism with the platform's own
  # rather than teaching an EBP chain to a machine that never had one.
  add_compile_options(-g)
else()
  message(FATAL_ERROR
    "No determinism flags are set for compiler '${CMAKE_CXX_COMPILER_ID}'.  The simulation must be "
    "compiled without floating-point contraction and with a signed char, or it desyncs; add this "
    "compiler's spelling of -ffp-contract=off and -fsigned-char before building with it.")
endif()

# ---- wwdebug --------------------------------------------------------------
# Libraries/Include is on the WWVegas targets from B3 onward: Platform/MSVCCompat.h lives
# there, always.h includes it, and every WWVegas library reaches always.h.  Nothing in that
# directory can shadow a WWVegas header - its only headers are under Lib/, rts/ and Platform/.
add_library(wwdebug STATIC
  ${WW}/WWDebug/wwdebug.cpp
  ${WW}/WWDebug/wwmemlog.cpp
  ${WW}/WWDebug/wwprofile.cpp)
target_include_directories(wwdebug PUBLIC ${WW}/WWDebug
  PRIVATE ${WW}/WWLib ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

# ---- wwutil ---------------------------------------------------------------
add_library(wwutil STATIC
  ${WW}/Wwutil/mathutil.cpp
  ${WW}/Wwutil/miscutil.cpp)
target_include_directories(wwutil PUBLIC ${WW}/Wwutil
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwutil PRIVATE wwdebug)

# ---- wwmath ---------------------------------------------------------------
add_library(wwmath STATIC
  ${WW}/WWMath/aabox.cpp          ${WW}/WWMath/aabtreecull.cpp
  ${WW}/WWMath/cardinalspline.cpp ${WW}/WWMath/catmullromspline.cpp
  ${WW}/WWMath/colmath.cpp        ${WW}/WWMath/colmathaabox.cpp
  ${WW}/WWMath/colmathaabtri.cpp  ${WW}/WWMath/colmathfrustum.cpp
  ${WW}/WWMath/colmathline.cpp    ${WW}/WWMath/colmathobbobb.cpp
  ${WW}/WWMath/colmathobbox.cpp   ${WW}/WWMath/colmathobbtri.cpp
  ${WW}/WWMath/colmathplane.cpp   ${WW}/WWMath/colmathsphere.cpp
  ${WW}/WWMath/cullsys.cpp        ${WW}/WWMath/curve.cpp
  ${WW}/WWMath/dettrig.cpp
  ${WW}/WWMath/euler.cpp          ${WW}/WWMath/frustum.cpp
  ${WW}/WWMath/gridcull.cpp       ${WW}/WWMath/hermitespline.cpp
  ${WW}/WWMath/lineseg.cpp        ${WW}/WWMath/lookuptable.cpp
  ${WW}/WWMath/matrix3.cpp        ${WW}/WWMath/matrix3d.cpp
  ${WW}/WWMath/matrix4.cpp        ${WW}/WWMath/obbox.cpp
  ${WW}/WWMath/ode.cpp            ${WW}/WWMath/pot.cpp
  ${WW}/WWMath/quat.cpp           ${WW}/WWMath/tcbspline.cpp
  ${WW}/WWMath/tri.cpp            ${WW}/WWMath/v3_rnd.cpp
  ${WW}/WWMath/vehiclecurve.cpp   ${WW}/WWMath/vp.cpp
  ${WW}/WWMath/wwmath.cpp)
target_include_directories(wwmath PUBLIC ${WW}/WWMath
  PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwmath PRIVATE wwdebug)

# ---- compression ----------------------------------------------------------
# zlib 1.1.4 (vendored, per README) minus maketree.c - that one is a table
# generator with its own main().  Lzhl_tcp.cpp is also skipped (socket layer,
# nothing in the game calls it).
add_library(compression STATIC
  ${COMP}/CompressionManager.cpp
  ${COMP}/EAC/btreeabout.cpp ${COMP}/EAC/btreedecode.cpp ${COMP}/EAC/btreeencode.cpp
  ${COMP}/EAC/huffabout.cpp  ${COMP}/EAC/huffdecode.cpp  ${COMP}/EAC/huffencode.cpp
  ${COMP}/EAC/refabout.cpp   ${COMP}/EAC/refdecode.cpp   ${COMP}/EAC/refencode.cpp
  ${COMP}/LZHCompress/NoxCompress.cpp
  ${COMP}/LZHCompress/CompLibSource/Huff.cpp
  ${COMP}/LZHCompress/CompLibSource/Lz.cpp
  ${COMP}/LZHCompress/CompLibSource/Lzhl.cpp
  ${COMP}/ZLib/adler32.c  ${COMP}/ZLib/compress.c ${COMP}/ZLib/crc32.c
  ${COMP}/ZLib/deflate.c  ${COMP}/ZLib/gzio.c     ${COMP}/ZLib/infblock.c
  ${COMP}/ZLib/infcodes.c ${COMP}/ZLib/inffast.c  ${COMP}/ZLib/inflate.c
  ${COMP}/ZLib/inftrees.c ${COMP}/ZLib/infutil.c  ${COMP}/ZLib/trees.c
  ${COMP}/ZLib/uncompr.c  ${COMP}/ZLib/zutil.c)
target_include_directories(compression
  PUBLIC ${COMP} ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  PRIVATE ${COMP}/LZHCompress/CompLibHeader)
# CompressionManager calls z_compress2/z_uncompress and BaseType.h owns 'Byte',
# so zlib must be built with its Z_PREFIX renames on.
target_compile_definitions(compression PRIVATE Z_PREFIX)
# LZH-Light's _lzhl.h says `#define UINT32 unsigned long`, which is 32 bits under Windows' LLP64
# and 64 under Darwin's LP64.  LZHASH and LZPOS are both UINT32, so at 64 bits the hash index runs
# off the end of its table and LZHLCompressor::compress walks into unmapped memory - a SIGBUS on the
# first buffer it is handed.  The header asks first (#ifndef UINT32), so saying it here fixes the
# width without patching a vendored file that A2 re-fetches.  These widths reach the compressed
# bytes that save games and network packets are made of, so this is a parity fix, not a tidy-up.
#
# Not on Windows, and the reason is not caution.  UINT32 and UINT16 are Windows SDK *typedefs* -
# basetsd.h has `typedef unsigned int UINT32, *PUINT32;` - and _lzhl.h's guard tests for a *macro*,
# so on Windows the guard is true either way and the header already defines `unsigned long`, which
# is already 32 bits there.  The define would buy nothing and cost something: NoxCompress.cpp
# includes Lib/BaseType.h, which includes <windef.h>, so a command-line UINT32 macro is live when
# the SDK's typedef is preprocessed and turns it into `typedef unsigned int unsigned int, *PUINT32;`.
# clang only warns at that and limps on; MSVC is stricter about duplicate declaration specifiers and
# neither machine here can tell us how much stricter.  Found by a contributor before it was merged.
if(ZH_PLATFORM_POSIX)
  target_compile_definitions(compression PRIVATE UINT32=unsigned\ int UINT16=unsigned\ short)
endif()

# Round-trip check over every codec CompressionManager exposes.
add_executable(compression_selfcheck ${COMP}/test_compression.cpp)
target_link_libraries(compression_selfcheck PRIVATE compression)

# DetRound::To_Long has to round half to even on every architecture, because its callers were written
# against an instruction that does.  Those callers are WW3D2's animation (hrawanim's floor idiom is
# arithmetic built on round-to-nearest) and WWMath - not GameLogic, which has none; an earlier
# version of this comment said "the simulation's callers" and was wrong.  Lib/DetRound.h is
# header-only and this links nothing, so it runs before any library compiles.  It checks DetRound
# against a hand-computed tie table on ONE architecture; it is not cross-architecture evidence.
# E3 (arch_differential) is.  See Tests/test_detround.cpp.
add_executable(detround_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_detround.cpp)
target_include_directories(detround_selfcheck PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)

# d3dxportable.h reproduces d3dx9_43.dll's D3DXVec4Transform bit for bit, because DumbProjectile-
# Behavior flies a shell along it and the flight is in the CRC.  Its expected values are outputs of
# Microsoft's own machine code (Tests/d3dx_golden.h), so this is not a self-consistency check.
# Header-only and links nothing, like detround_selfcheck.  See Tests/test_d3dxportable.cpp.
add_executable(d3dxportable_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dxportable.cpp)
target_include_directories(d3dxportable_selfcheck PRIVATE ${WW}/WW3D2 ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Guards the hand-rolled Matrix3D::Get_Inverse that replaced D3DXMatrixInverse.  It checks that the
# inverse is right, including one exact answer; it is NOT determinism evidence, and its header says
# why.  E3 is the cross-architecture evidence for DetTrig.
#
# wwlib and wwsaveload on every platform.  test_inverse.cpp needs only Matrix3D, but the link does
# not stop there: quat.cpp needs _FastAcosTable, which pulls in wwmath.cpp, and wwmath.cpp's
# WWMath::Init and Do_Force_Links reach LookupTableMgrClass (wwlib's file factory, strings, chunk
# I/O) and every spline's persist factory (wwsaveload).  ld -why_load shows the chain.  A macOS line
# with wwdebug alone was here once, on the belief that wwmath resolved against it; it had never been
# linked, and it does not.  So on macOS this waits for wwlib and wwsaveload to build.
add_executable(wwmath_selfcheck ${WW}/WWMath/test_inverse.cpp)
target_include_directories(wwmath_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)
target_link_libraries(wwmath_selfcheck PRIVATE wwmath wwsaveload wwlib wwdebug)

# ---- wwlib ----------------------------------------------------------------
set(WWLIB_SOURCES
  ${WW}/WWLib/_convert.cpp   ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/argv.cpp
  ${WW}/WWLib/b64pipe.cpp    ${WW}/WWLib/b64straw.cpp   ${WW}/WWLib/base64.cpp
  ${WW}/WWLib/blit.cpp       ${WW}/WWLib/blowfish.cpp   ${WW}/WWLib/buff.cpp
  ${WW}/WWLib/bufffile.cpp   ${WW}/WWLib/chunkio.cpp    ${WW}/WWLib/convert.cpp
  ${WW}/WWLib/cpudetect.cpp  ${WW}/WWLib/crc.cpp        ${WW}/WWLib/cstraw.cpp
  ${WW}/WWLib/data.cpp       ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
  ${WW}/WWLib/Except.cpp     ${WW}/WWLib/FastAllocator.cpp
  ${WW}/WWLib/ffactory.cpp   ${WW}/WWLib/fixed.cpp      ${WW}/WWLib/md5.cpp
  ${WW}/WWLib/gcd_lcm.cpp    ${WW}/WWLib/hash.cpp
  ${WW}/WWLib/hsv.cpp        ${WW}/WWLib/ini.cpp        ${WW}/WWLib/int.cpp
  ${WW}/WWLib/jshell.cpp     ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp
  ${WW}/WWLib/lcw.cpp        ${WW}/WWLib/load.cpp       ${WW}/WWLib/mixfile.cpp
  ${WW}/WWLib/mono.cpp       ${WW}/WWLib/mpmath.cpp     ${WW}/WWLib/mpu.cpp
  ${WW}/WWLib/msgloop.cpp    ${WW}/WWLib/multilist.cpp  ${WW}/WWLib/mutex.cpp
  ${WW}/WWLib/nstrdup.cpp    ${WW}/WWLib/palette.cpp    ${WW}/WWLib/pcx.cpp
  ${WW}/WWLib/pipe.cpp       ${WW}/WWLib/pk.cpp         ${WW}/WWLib/ramfile.cpp
  ${WW}/WWLib/random.cpp     ${WW}/WWLib/rawfile.cpp    ${WW}/WWLib/rc4.cpp
  ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/readline.cpp   ${WW}/WWLib/realcrc.cpp
  ${WW}/WWLib/refcount.cpp   ${WW}/WWLib/registry.cpp   ${WW}/WWLib/rgb.cpp
  ${WW}/WWLib/rle.cpp        ${WW}/WWLib/rndstrng.cpp   ${WW}/WWLib/sampler.cpp
  ${WW}/WWLib/sha.cpp        ${WW}/WWLib/slnode.cpp     ${WW}/WWLib/srandom.cpp
  ${WW}/WWLib/stimer.cpp     ${WW}/WWLib/straw.cpp      ${WW}/WWLib/strtok_r.cpp
  ${WW}/WWLib/systimer.cpp   ${WW}/WWLib/tagblock.cpp   ${WW}/WWLib/TARGA.CPP
  ${WW}/WWLib/textfile.cpp   ${WW}/WWLib/tgatodxt.cpp   ${WW}/WWLib/thread.cpp
  ${WW}/WWLib/trim.cpp       ${WW}/WWLib/vector.cpp     ${WW}/WWLib/verchk.cpp
  ${WW}/WWLib/widestring.cpp ${WW}/WWLib/win.cpp        ${WW}/WWLib/WWCOMUtil.cpp
  ${WW}/WWLib/wwfile.cpp     ${WW}/WWLib/wwfont.cpp     ${WW}/WWLib/wwstring.cpp
  ${WW}/WWLib/xpipe.cpp      ${WW}/WWLib/xstraw.cpp     ${WW}/WWLib/xsurface.cpp)
# Win32 subsystem code, left out of wwlib everywhere but Windows.  Each of these is a whole file of
# one Windows API rather than a type leaking out of one, so there is nothing to port short of
# reimplementing the API.  On Windows this list is not read, and wwlib gets the same sources in the
# same order as before.  Put one back only with a caller that needs it.
#
# How "no caller" was established, because the first way was wrong: B5 searched for includers of
# each header, and that cannot see a function-local extern - verchk.cpp's, below.  Re-audited
# 2026-09-25 by symbol: every function, class and global each file defines, searched as a whole word
# through every tracked source outside Tools/, and each hit read.  Apart from RegistryClass (callers
# named below) and verchk (fixed), every hit was a comment, a string, a commented-out block, a type
# named only by pointer or reference, or code under a macro nothing defines (Except.cpp's
# DEMO_TIME_OUT).  What that cannot see: a name built by token pasting, and vendored code, which is
# not tracked and does not use WWLib.
if(ZH_PLATFORM_POSIX)
  list(REMOVE_ITEM WWLIB_SOURCES
    # DirectDraw, the pre-Direct3D 2D surface API.  The renderer never reaches it; DSurface is named
    # nowhere outside WWLib, and draw.cpp, the fourth file of the set, was already out.  ConvertClass
    # survives as a type: wwfont.cpp uses only its inline Convert_Pixel, WW3D2's txt*.cpp only
    # references to it.
    ${WW}/WWLib/convert.cpp    ${WW}/WWLib/ddraw.cpp      ${WW}/WWLib/dsurface.cpp
    # The \\.\MONO device driver for a second, monochrome debug monitor.  WW3D2's rendobj.cpp
    # includes _mono.h and names nothing from it.
    ${WW}/WWLib/_mono.cpp      ${WW}/WWLib/mono.cpp
    # Win32 resources, the message pump and its keyboard, ShellExecute, COM IDispatch and version
    # resources.  No callers at all.
    ${WW}/WWLib/data.cpp       ${WW}/WWLib/rcfile.cpp     ${WW}/WWLib/msgloop.cpp
    ${WW}/WWLib/keyboard.cpp   ${WW}/WWLib/LaunchWeb.cpp  ${WW}/WWLib/WWCOMUtil.cpp
    # verchk.cpp had one caller after all: Wwutil's Get_File_Id_String declares its
    # Get_Image_File_Header extern inside the function, so a search for verchk.h misses it.  That
    # call is Windows-only now; off Windows the timestamp is 0.
    ${WW}/WWLib/verchk.cpp
    # The HINSTANCE/HWND globals, which win.h declares only under _WINDOWS.
    ${WW}/WWLib/win.cpp
    # RegistryClass is HKEY throughout.  Its callers are dx8wrapper.cpp, W3DDisplay.cpp and
    # WWAudio.cpp, which persist device and audio settings; where those live on macOS is theirs
    # to decide, and registry.h says so to anyone who includes it.
    ${WW}/WWLib/registry.cpp
    # SecureRandomClass has no caller, and its only non-Windows seed is the _UNIX arm, which the
    # plan forbids switching on.
    ${WW}/WWLib/srandom.cpp
    # Get_CPU_Rate, Get_CPU_Clock, Get_RDTSC_CPU_Speed and RDTSC: an x86 time-stamp counter timed
    # against QueryPerformanceCounter.  Nothing calls any of the four - cpudetect.cpp and Except.cpp
    # include mpu.h and use nothing from it, and cpudetect reads __rdtsc itself.
    ${WW}/WWLib/mpu.cpp)
  # And one POSIX-only file: the engine's paths resolved against a POSIX file system, and the zh_*
  # forwarders built on it (C1, decisions D1 and D4).  WWLib because RawFileClass and GameEngine both
  # link it, and the resolver has to work before any engine subsystem exists.
  list(APPEND WWLIB_SOURCES ${WW}/WWLib/posixpath.cpp)
endif()
add_library(wwlib STATIC ${ZH_NOT_YET_PORTABLE} ${WWLIB_SOURCES})
target_include_directories(wwlib PUBLIC ${WW}/WWLib
  PRIVATE ${WW}/WWMath ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwlib PRIVATE wwdebug)
if(ZH_PLATFORM_WINDOWS)
  # winmm: timeGetTime/timeBeginPeriod.  version: verchk.cpp's GetFileVersionInfo,
  # which Wwutil drags in for Get_File_Id_String.  Both are Windows import libraries; B2 replaces
  # the first with a platform clock and B3 the second.
  target_link_libraries(wwlib PUBLIC winmm version)
endif()

# ---- wwsaveload -----------------------------------------------------------
add_library(wwsaveload STATIC ${ZH_NOT_YET_PORTABLE}
  ${WW}/WWSaveLoad/definition.cpp          ${WW}/WWSaveLoad/definitionfactory.cpp
  ${WW}/WWSaveLoad/definitionfactorymgr.cpp ${WW}/WWSaveLoad/definitionmgr.cpp
  ${WW}/WWSaveLoad/parameter.cpp           ${WW}/WWSaveLoad/persistfactory.cpp
  ${WW}/WWSaveLoad/pointerremap.cpp        ${WW}/WWSaveLoad/saveload.cpp
  ${WW}/WWSaveLoad/saveloadstatus.cpp      ${WW}/WWSaveLoad/saveloadsubsystem.cpp
  ${WW}/WWSaveLoad/twiddler.cpp            ${WW}/WWSaveLoad/wwsaveload.cpp)
target_include_directories(wwsaveload PUBLIC ${WW}/WWSaveLoad
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/Wwutil ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include)
target_link_libraries(wwsaveload PRIVATE wwdebug wwlib wwmath)

# ---- WW3D2's sources, on every platform ------------------------------------
# The renderer core is the same files on Windows and off it (decision 7: off Windows it speaks D3D9 to
# a device of our own).  Three are Windows's alone, and its block adds them back: FramGrab.cpp (AVI
# capture through vfw32), dx8webbrowser.cpp (the embedded browser, an MSVC #import) and dx11runtime.cpp
# (the Direct3D 11 backend's switchboard; dx11runtime_posix.cpp answers for it off Windows).
set(ZH_WW3D2_SOURCES
  ${WW}/WW3D2/aabtree.cpp ${WW}/WW3D2/aabtreebuilder.cpp ${WW}/WW3D2/agg_def.cpp
  ${WW}/WW3D2/animatedsoundmgr.cpp ${WW}/WW3D2/animobj.cpp ${WW}/WW3D2/assetmgr.cpp
  ${WW}/WW3D2/assetstatus.cpp ${WW}/WW3D2/bitmaphandler.cpp ${WW}/WW3D2/bmp2d.cpp
  ${WW}/WW3D2/boxrobj.cpp ${WW}/WW3D2/bwrender.cpp ${WW}/WW3D2/camera.cpp
  ${WW}/WW3D2/collect.cpp ${WW}/WW3D2/coltest.cpp ${WW}/WW3D2/composite.cpp
  ${WW}/WW3D2/dazzle.cpp ${WW}/WW3D2/ddsfile.cpp ${WW}/WW3D2/decalmsh.cpp
  ${WW}/WW3D2/decalsys.cpp ${WW}/WW3D2/distlod.cpp ${WW}/WW3D2/dx8caps.cpp
  ${WW}/WW3D2/dx8fvf.cpp ${WW}/WW3D2/dx8indexbuffer.cpp ${WW}/WW3D2/dx8polygonrenderer.cpp
  ${WW}/WW3D2/dx8renderer.cpp ${WW}/WW3D2/dx8rendererdebugger.cpp ${WW}/WW3D2/dx8texman.cpp
  ${WW}/WW3D2/dx8vertexbuffer.cpp ${WW}/WW3D2/dx8wrapper.cpp ${WW}/WW3D2/dynamesh.cpp
  ${WW}/WW3D2/ffprobe.cpp ${WW}/WW3D2/ffshadercache.cpp ${WW}/WW3D2/font3d.cpp
  ${WW}/WW3D2/formconv.cpp ${WW}/WW3D2/hanim.cpp ${WW}/WW3D2/hanimmgr.cpp
  ${WW}/WW3D2/hcanim.cpp ${WW}/WW3D2/hlod.cpp ${WW}/WW3D2/hmdldef.cpp
  ${WW}/WW3D2/hmorphanim.cpp ${WW}/WW3D2/hrawanim.cpp ${WW}/WW3D2/htree.cpp
  ${WW}/WW3D2/htreemgr.cpp ${WW}/WW3D2/intersec.cpp ${WW}/WW3D2/layer.cpp
  ${WW}/WW3D2/light.cpp ${WW}/WW3D2/lightenvironment.cpp ${WW}/WW3D2/line3d.cpp
  ${WW}/WW3D2/linegrp.cpp ${WW}/WW3D2/mapper.cpp ${WW}/WW3D2/matinfo.cpp
  ${WW}/WW3D2/matpass.cpp ${WW}/WW3D2/matrixmapper.cpp ${WW}/WW3D2/mesh.cpp
  ${WW}/WW3D2/meshbuild.cpp ${WW}/WW3D2/meshdam.cpp ${WW}/WW3D2/meshgeometry.cpp
  ${WW}/WW3D2/meshmatdesc.cpp ${WW}/WW3D2/meshmdl.cpp ${WW}/WW3D2/meshmdlio.cpp
  ${WW}/WW3D2/metalmap.cpp ${WW}/WW3D2/missingtexture.cpp ${WW}/WW3D2/motchan.cpp
  ${WW}/WW3D2/nullrobj.cpp ${WW}/WW3D2/part_buf.cpp ${WW}/WW3D2/part_emt.cpp
  ${WW}/WW3D2/part_ldr.cpp ${WW}/WW3D2/pivot.cpp ${WW}/WW3D2/pointgr.cpp
  ${WW}/WW3D2/polyinfo.cpp ${WW}/WW3D2/predlod.cpp ${WW}/WW3D2/prim_anim.cpp
  ${WW}/WW3D2/projector.cpp ${WW}/WW3D2/proto.cpp ${WW}/WW3D2/render2d.cpp
  ${WW}/WW3D2/render2dsentence.cpp ${WW}/WW3D2/renderobjectrecycler.cpp ${WW}/WW3D2/rendobj.cpp
  ${WW}/WW3D2/rinfo.cpp ${WW}/WW3D2/ringobj.cpp ${WW}/WW3D2/scene.cpp
  ${WW}/WW3D2/segline.cpp ${WW}/WW3D2/seglinerenderer.cpp ${WW}/WW3D2/shader.cpp
  ${WW}/WW3D2/shattersystem.cpp ${WW}/WW3D2/snappts.cpp ${WW}/WW3D2/sortingrenderer.cpp
  ${WW}/WW3D2/soundrobj.cpp ${WW}/WW3D2/sphereobj.cpp ${WW}/WW3D2/static_sort_list.cpp
  ${WW}/WW3D2/statistics.cpp ${WW}/WW3D2/streak.cpp ${WW}/WW3D2/streakRender.cpp
  ${WW}/WW3D2/stripoptimizer.cpp ${WW}/WW3D2/surfaceclass.cpp ${WW}/WW3D2/texfcach.cpp
  ${WW}/WW3D2/texproject.cpp ${WW}/WW3D2/texture.cpp ${WW}/WW3D2/texturefilter.cpp
  ${WW}/WW3D2/textureloader.cpp ${WW}/WW3D2/texturethumbnail.cpp ${WW}/WW3D2/txt.cpp
  ${WW}/WW3D2/vertmaterial.cpp ${WW}/WW3D2/visrasterizer.cpp ${WW}/WW3D2/w3d_dep.cpp
  ${WW}/WW3D2/w3d_util.cpp ${WW}/WW3D2/w3dexclusionlist.cpp ${WW}/WW3D2/ww3d.cpp
  ${WW}/WW3D2/ww3dformat.cpp
)

# ===========================================================================
# Windows only, from here down to dx9_smoke: every target in this block links d3d9, d3d11 or dxgi,
# or is built out of something that does.  Nothing is removed and nothing is stubbed - tracks D2
# through D5 abstract the backend interface and add a Metal one behind the same seam, and they
# bring these back one at a time.  Left unindented so the diff that added the guard is the guard.
# ===========================================================================
if(ZH_PLATFORM_WINDOWS)

# ---- d3dx9runtime ---------------------------------------------------------
# The D3DX9 entry points the renderer binds at run time, in a library of their own
# because GameEngine needs the math half without the renderer: BezierSegment carries
# D3DXMATRIX and DumbProjectileBehavior steers a shell with it.
add_library(d3dx9runtime STATIC
  ${WW}/WW3D2/d3dx9runtime.cpp ${WW}/WW3D2/d3d8shadertranslate.cpp)
set_target_properties(d3dx9runtime PROPERTIES CXX_STANDARD 17)
target_include_directories(d3dx9runtime PUBLIC ${WW}/WW3D2)
target_link_libraries(d3dx9runtime PUBLIC d3d9)

# ---- ffshader -------------------------------------------------------------
# The fixed-function texture stages written out as HLSL, phase 2's replacement for
# combiners D3D11 does not have.  Its own library because it is string building over
# a description and nothing else: the test links this alone rather than all of WW3D2.
add_library(ffshader STATIC ${WW}/WW3D2/ffshader.cpp)
set_target_properties(ffshader PROPERTIES CXX_STANDARD 17)
target_include_directories(ffshader PUBLIC ${WW}/WW3D2)
target_link_libraries(ffshader PUBLIC d3d9)

# ---- ffvertex -------------------------------------------------------------
# The other half of the same job: the fixed-function vertex pipeline as HLSL.  It
# generates for both profiles so the D3D9 form can be compared against the pipeline
# it replaces before the D3D11 one is trusted.
add_library(ffvertex STATIC ${WW}/WW3D2/ffvertex.cpp)
set_target_properties(ffvertex PROPERTIES CXX_STANDARD 17)
target_include_directories(ffvertex PUBLIC ${WW}/WW3D2)
target_link_libraries(ffvertex PUBLIC d3d9)

# ---- engineshader ---------------------------------------------------------
# The shipped .vso and .pso files transcribed as HLSL, for the draws that bind one
# of the engine's own shaders and set no fixed-function state a generator could read.
# Its own library for the same reason ffvertex is: string building over an enum, and
# the test compiles what it writes without a device anywhere near it.
add_library(engineshader STATIC ${WW}/WW3D2/engineshader.cpp)
set_target_properties(engineshader PROPERTIES CXX_STANDARD 17)
target_include_directories(engineshader PUBLIC ${WW}/WW3D2)
target_link_libraries(engineshader PUBLIC ffshader)

# ---- dx11device -----------------------------------------------------------
# The Direct3D 11 device, its swap chain and the views on it: the bottom of phase 2.
# Its own library for the same reason ffshader is, and for one more - it links d3d11
# and dxgi, and nothing else in the tree does yet.  The test creates a real device.
add_library(dx11device STATIC ${WW}/WW3D2/dx11device.cpp)
set_target_properties(dx11device PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11device PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11device PUBLIC d3d11 dxgi)

# ---- dx11state ------------------------------------------------------------
# D3D9's render states turned into D3D11's three state objects.  Pure translation,
# no device, so the whole table is checked by a test rather than looked at on screen.
add_library(dx11state STATIC ${WW}/WW3D2/dx11state.cpp)
set_target_properties(dx11state PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11state PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11state PUBLIC d3d9 d3d11)

# ---- dx11layout -----------------------------------------------------------
# A D3D9 flexible vertex format read out as a D3D11 input layout.  The vertex buffers
# keep the bytes they have; this is what tells D3D11 where the fields are.
add_library(dx11layout STATIC ${WW}/WW3D2/dx11layout.cpp)
set_target_properties(dx11layout PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11layout PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11layout PUBLIC d3d9 d3d11)

# ---- dx11resource ---------------------------------------------------------
# The buffers and textures: D3D9's formats, pools and lock flags read as D3D11's
# formats, usages and map types.  Half a table and half what the device will accept,
# so the test checks the table and then hands the results to a real device.
add_library(dx11resource STATIC ${WW}/WW3D2/dx11resource.cpp)
set_target_properties(dx11resource PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11resource PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11resource PUBLIC d3d9 d3d11)

# ---- dx11twin -------------------------------------------------------------
# The D3D11 copy of a vertex or index buffer and the heap block both copies are
# written through.  A D3D9 buffer is write-only, so the copy cannot be built after
# the fact and the lock has to be moved instead; the test proves the same bytes end
# up on both sides.
add_library(dx11twin STATIC ${WW}/WW3D2/dx11twin.cpp)
set_target_properties(dx11twin PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11twin PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11twin PUBLIC dx11resource d3d11)

# ---- dx11texture ----------------------------------------------------------
# The D3D11 copy of a texture the engine already built in D3D9, read back off the
# managed original at the first bind and kept on it as private data.
add_library(dx11texture STATIC ${WW}/WW3D2/dx11texture.cpp)
set_target_properties(dx11texture PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11texture PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11texture PUBLIC dx11resource d3d9 d3d11 dxguid)

# ---- dx11post -------------------------------------------------------------
# The offscreen render target between the scene and the swap chain, and the full
# screen passes over it.  Depends on the device and on nothing else: it reads a
# texture and writes a render target, and knows nothing about the state
# translation the rest of the backend is.
add_library(dx11post STATIC ${WW}/WW3D2/dx11post.cpp)
set_target_properties(dx11post PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11post PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11post PUBLIC dx11device d3d11)

# ---- dx11backend ----------------------------------------------------------
# The renderer itself: the engine's D3D9-shaped calls taken as they are and resolved
# into a D3D11 pipeline at the moment of the draw.  Ties the five pieces above
# together, which is why it is the last of them and the one with a drawing test.
add_library(dx11backend STATIC ${WW}/WW3D2/dx11backend.cpp)
set_target_properties(dx11backend PROPERTIES CXX_STANDARD 17)
target_include_directories(dx11backend PUBLIC ${WW}/WW3D2)
target_link_libraries(dx11backend PUBLIC dx11device dx11state dx11layout dx11resource
  engineshader ffshader ffvertex)

# ---- ww3d2 (renderer core) ------------------------------------------------
# wwshade is header-only here on purpose: no .dsp links wwshade.lib and its
# shd*bump* sources need shdpp/NVASM-generated headers.  Revisit at exe link.
# dx8webbrowser.cpp stays inert (ENABLE_EMBEDDED_BROWSER unset).
add_library(ww3d2 STATIC
  ${WW}/WW3D2/FramGrab.cpp ${WW}/WW3D2/dx8webbrowser.cpp ${WW}/WW3D2/dx11runtime.cpp
  ${ZH_WW3D2_SOURCES}
)
target_include_directories(ww3d2
  PUBLIC ${WW}/WW3D2
  PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
          ${WW}/wwshade ${WW}/WWAudio ${WW}/Miles6/include)
# RENDERER-ROADMAP.md phase 1: d3d9.h and d3d9.lib come out of the Windows SDK, so
# the vendored DirectX 8 headers are off the include path entirely - a d3d8.h left
# reachable is a file that compiles against the wrong runtime without saying so.
# D3DX9 has no import library anywhere and is bound at runtime instead: see
# WW3D2/d3dx9runtime.h.  vfw32 is FramGrab.cpp writing AVIs.
target_link_libraries(ww3d2
  PUBLIC d3d9 dxguid vfw32 d3dx9runtime ffshader dx11backend dx11post dx11twin dx11texture
  PRIVATE wwlib wwmath wwdebug wwsaveload)
# dx8webbrowser.cpp is the one file /MP cannot have: MSVC rejects #import under
# multi-process compilation (C2813) and finds the directive by a lexical scan, so
# even though ENABLE_EMBEDDED_BROWSER's block is what holds it, disabling the block
# would not help.  /MP1 puts just this file back on a single process.
set_source_files_properties(${WW}/WW3D2/dx8webbrowser.cpp PROPERTIES COMPILE_OPTIONS /MP1)

# Phase 8: 1 kHz EIP sampler that symbolizes against the linker map - the
# machine has no debugger, so this is how hotspots get names.
add_executable(eip_sampler ${CMAKE_CURRENT_SOURCE_DIR}/Tools/ProfileSampler/eip_sampler.cpp)
set_target_properties(eip_sampler PROPERTIES CXX_STANDARD 17)

# RENDERER-ROADMAP.md phase 1 checkpoint: a real device, with no translating dll
# anywhere.  It replaces dx8_smoke, which proved the same thing about the vendored
# DirectX 8 SDK and the d3d8.dll the renderer used to load; that path is gone, and
# test_dx8_smoke.cpp stays in the tree unbuilt as the record of what it measured.
# d3d9.h and d3d9.lib come from the Windows SDK, so this target never sees
# Libraries/DirectX/Include.
add_executable(dx9_smoke ${WW}/WW3D2/test_dx9_smoke.cpp)
target_link_libraries(dx9_smoke PRIVATE d3dx9runtime user32)
# Tests/ for d3dx_golden.h: dx9_smoke prints what this machine's d3dx9_43.dll computes for
# B17's golden rows, and which of its CPU-dispatched bodies ran.
target_include_directories(dx9_smoke PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests)
endif()  # ZH_PLATFORM_WINDOWS - renderer, Direct3D 9/11 backends, dx9_smoke

# ===========================================================================
# Phase 2 - remaining support libs.  None of these touch DX8.
# ===========================================================================
set(LIBSRC ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Source)

# The next three link Windows import libraries and nothing else stands in for them yet: debug wants
# dbghelp and comctl32, profile is built on debug, and wwdownload is FTP over winsock.  C5 replaces
# the crash reporting and B6 decides what wwdownload becomes on macOS.
if(ZH_PLATFORM_WINDOWS)
# ---- debug ----------------------------------------------------------------
# EA's in-repo debug/logging lib (RTS.dsp links it as debug.lib).  Self-contained
# apart from the Win32 SDK.  The test1-6/, netserv/ and debug_dlg/ subdirs are
# separate .dsp projects, not part of the lib.
add_library(debug STATIC
  ${LIBSRC}/debug/debug.cpp        ${LIBSRC}/debug/debug_cmd.cpp
  ${LIBSRC}/debug/debug_debug.cpp  ${LIBSRC}/debug/debug_except.cpp
  ${LIBSRC}/debug/debug_getdefaultcommands.cpp
  ${LIBSRC}/debug/debug_internal.cpp
  ${LIBSRC}/debug/debug_io_con.cpp ${LIBSRC}/debug/debug_io_flat.cpp
  ${LIBSRC}/debug/debug_io_net.cpp ${LIBSRC}/debug/debug_io_ods.cpp
  ${LIBSRC}/debug/debug_purecall.cpp
  ${LIBSRC}/debug/debug_stack.cpp)
target_include_directories(debug PUBLIC ${LIBSRC}/debug)
target_link_libraries(debug PUBLIC dbghelp comctl32 ws2_32)
# "debug" is also a target_link_libraries config keyword, so it can never be
# passed as a plain library name.  Link this alias instead.
add_library(debuglib ALIAS debug)

# ---- profile --------------------------------------------------------------
# EA's in-repo sampling/function profiler.  GameEngine includes profile.h, whose
# #pragma comment(lib, "profile.lib") makes this a hard link dependency even
# though almost nothing calls into it.
add_library(profile STATIC
  ${LIBSRC}/profile/profile.cpp           ${LIBSRC}/profile/profile_cmd.cpp
  ${LIBSRC}/profile/profile_funclevel.cpp ${LIBSRC}/profile/profile_highlevel.cpp
  ${LIBSRC}/profile/profile_result.cpp)
target_include_directories(profile PUBLIC ${LIBSRC}/profile)
target_link_libraries(profile PUBLIC debuglib)

# ---- wwdownload -----------------------------------------------------------
# FTP patch downloader.  Its wlib/resource.h includes are all commented out in
# the shipped source, so it needs nothing but winsock.
add_library(wwdownload STATIC
  ${WW}/WWDownload/Download.cpp ${WW}/WWDownload/FTP.CPP
  ${WW}/WWDownload/registry.cpp ${WW}/WWDownload/urlBuilder.cpp)
target_include_directories(wwdownload PUBLIC ${WW}/WWDownload ${WW})
target_link_libraries(wwdownload PUBLIC wsock32 winmm)
endif()  # ZH_PLATFORM_WINDOWS - debug, profile, wwdownload

# ---- benchmark ------------------------------------------------------------
# BYTEmark sources are missing; benchmark_stub.c stands in for them.  See the
# file header for why a constant is enough and when to revisit.
add_library(benchmark STATIC ${LIBSRC}/Benchmark/benchmark_stub.c)
target_include_directories(benchmark PUBLIC ${LIBSRC}/Benchmark)

# ---- eabrowserdispatch ----------------------------------------------------
# MIDL is a Windows SDK tool and this whole library is its output, so there is nothing to generate
# on a Mac.  B6 puts a null WebBrowser in Stubs/ for the macOS side.
if(ZH_PLATFORM_WINDOWS)
# Only BrowserDispatch.idl shipped; the .h/_i.c that GameEngine's WebBrowser.h
# expects are MIDL output.  Generate them at build time (MSBuild puts cl.exe,
# MIDL's preprocessor, on PATH) into a dir laid out so the shipped
# #include "EABrowserDispatch/BrowserDispatch.h" resolves unchanged.
set(EABD_GEN ${CMAKE_CURRENT_BINARY_DIR}/generated/EABrowserDispatch)
file(MAKE_DIRECTORY ${EABD_GEN})
# midl.exe ships with the Windows SDK, not on PATH at configure time.
file(GLOB MIDL_HINTS "$ENV{ProgramFiles\(x86\)}/Windows Kits/10/bin/*/x86")
find_program(MIDL_EXECUTABLE midl HINTS ${MIDL_HINTS} REQUIRED)
add_custom_command(
  OUTPUT ${EABD_GEN}/BrowserDispatch.h ${EABD_GEN}/BrowserDispatch_i.c
  COMMAND ${MIDL_EXECUTABLE} /nologo /env win32 /win32
          /h ${EABD_GEN}/BrowserDispatch.h
          /iid ${EABD_GEN}/BrowserDispatch_i.c
          /proxy ${EABD_GEN}/BrowserDispatch_p.c
          /dlldata ${EABD_GEN}/dlldata.c
          /tlb ${EABD_GEN}/BrowserDispatch.tlb
          ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  DEPENDS ${LIBSRC}/EABrowserDispatch/BrowserDispatch.idl
  COMMENT "MIDL BrowserDispatch.idl")
add_library(eabrowserdispatch STATIC
  ${EABD_GEN}/BrowserDispatch_i.c ${EABD_GEN}/BrowserDispatch.h)
target_include_directories(eabrowserdispatch
  PUBLIC ${CMAKE_CURRENT_BINARY_DIR}/generated)
endif()  # ZH_PLATFORM_WINDOWS - MIDL-generated browser dispatch

# ---- gamespy --------------------------------------------------------------
# Every platform (B6): the SDK is CMake-based and portable, and gameengine's online code - PeerThread,
# BuddyThread, PersistentStorageThread, MainMenuUtils - calls 89 of its entry points.  Built for real
# rather than stubbed, with Tools/vendor.sh's two patches applied (gamespy-strlwr-c-only.patch, and
# gamespy-gsi-unix.patch, which renames its _UNIX to GSI_UNIX so no engine file sees _UNIX).  Nothing
# in it runs until a player opens the online menus, and the services it would reach are gone.
# PLAN.md called for hand-written stubs behind the five GameSpy* libs, but the
# real SDK is open source (TheSuperHackers/GamespySDK, itself CMake-based), so
# vendor and build it instead: correct signatures, structs and enum values
# rather than 73 guessed prototypes.  Multiplayer stays out of scope - this only
# has to compile and link; nothing needs to reach a live GameSpy server.
# OpenSSL/HTTPS off so the SDK does not FetchContent OpenSSL for services that
# have been dead since 2014; voice and tests off for the same reason.
set(GS_OPENSSL          OFF CACHE BOOL "" FORCE)
set(GS_SECURESECONNECT  OFF CACHE BOOL "" FORCE)
set(GS_BUILD_TESTS      OFF CACHE BOOL "" FORCE)
set(GS_INCLUDE_VOICE    OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBSRC}/GameSpy ${CMAKE_CURRENT_BINARY_DIR}/gamespy)

# ---- litehtml -------------------------------------------------------------
# The HTML and CSS layout engine behind the pages drawn over the battlefield (GameClient/
# HtmlOverlay.cpp).  It lays a page out and calls back to draw each box and each run of text, so
# the drawing stays the game's own.  Its own CMakeLists sets /permissive- and C++17 for itself
# alone, and brings the gumbo HTML parser with it.
set(LITEHTML_BUILD_TESTING OFF CACHE BOOL "" FORCE)
add_subdirectory(${LIBSRC}/litehtml ${CMAKE_CURRENT_BINARY_DIR}/litehtml)

# ---- SDL3 and miniaudio: the platform layer off Windows (decision 3) -------------------------------
# Both are fetched by Tools/vendor.sh and neither is fetched on Windows, which keeps Win32Device and
# Miles.  Nothing links either yet: C2/C3 take SDL3's window and events, D4 its GPU API, C4 miniaudio.
# They are built by default all the same, so that a break shows up in the build that caused it
# rather than in the first task to link them.
if(ZH_PLATFORM_POSIX)
  # SDL3 static, and only the subsystems decision 3 uses.  On: video (window, events, message boxes)
  # and GPU (the renderer - Metal on macOS, Vulkan on Linux).  Off: audio (miniaudio has that job),
  # render (SDL's 2D renderer; D4 uses the GPU API directly), camera, joystick, haptic, hidapi,
  # power, sensor, dialog and tray, and the OpenGL/GLES video drivers, which the GPU API does not
  # use.  C3 turns joystick back on if it wants gamepads.  Tests, examples and install are off.
  foreach(_sdl_off SDL_SHARED SDL_TEST_LIBRARY SDL_TESTS SDL_EXAMPLES SDL_INSTALL SDL_UNINSTALL
                   SDL_AUDIO SDL_RENDER SDL_CAMERA SDL_JOYSTICK SDL_HAPTIC SDL_HIDAPI SDL_POWER
                   SDL_SENSOR SDL_DIALOG SDL_TRAY SDL_OPENGL SDL_OPENGLES)
    set(${_sdl_off} OFF CACHE BOOL "" FORCE)
  endforeach()
  foreach(_sdl_on SDL_STATIC SDL_VIDEO SDL_GPU)
    set(${_sdl_on} ON CACHE BOOL "" FORCE)
  endforeach()
  add_subdirectory(${LIBSRC}/SDL3 ${CMAKE_CURRENT_BINARY_DIR}/SDL3)

  # miniaudio: one C file.  Its own CMakeLists builds extras and examples this does not want.
  # It opens the platform's audio libraries at run time by default, so the static library needs
  # nothing at link beyond threads, libm and libdl, which are its consumers' to carry.
  find_package(Threads REQUIRED)
  add_library(miniaudio STATIC ${LIBSRC}/miniaudio/miniaudio.c)
  target_include_directories(miniaudio PUBLIC ${LIBSRC}/miniaudio)
  # libm on every POSIX system: separate from libc on Linux and the BSDs, part of libSystem on macOS,
  # where -lm is accepted and does nothing.  Not keyed on Linux, so the next platform needs no edit.
  target_link_libraries(miniaudio INTERFACE Threads::Threads ${CMAKE_DL_LIBS} m)

  # ---- The shader generators and what compiles their SDL3 GPU target (decision 4, D3) -----------
  # The generators are string building over a description; since ffstate.h they need nothing from
  # the DirectX SDK, so off Windows they are the same three libraries without d3d9.
  add_library(ffshader STATIC ${WW}/WW3D2/ffshader.cpp)
  target_include_directories(ffshader PUBLIC ${WW}/WW3D2)
  add_library(ffvertex STATIC ${WW}/WW3D2/ffvertex.cpp)
  target_include_directories(ffvertex PUBLIC ${WW}/WW3D2)
  add_library(engineshader STATIC ${WW}/WW3D2/engineshader.cpp)
  target_include_directories(engineshader PUBLIC ${WW}/WW3D2)
  target_link_libraries(engineshader PUBLIC ffshader)

  # glslang (HLSL to SPIR-V) and SPIRV-Cross (SPIR-V to MSL), fetched and pinned by vendor.sh, with
  # only the parts decision 4 uses.  Their options are set inside a function so they stay out of the
  # rest of this file, and both are EXCLUDE_FROM_ALL: what gets built is what links against them.
  function(zh_add_shader_compilers)
    # Both read options with the old CMP0077 behaviour, under which option() ignores a normal
    # variable its parent set.  Without this, every setting below would silently not apply.
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    set(ENABLE_HLSL ON)
    set(ENABLE_OPT OFF)
    set(ENABLE_SPIRV ON)
    set(ENABLE_GLSLANG_BINARIES OFF)
    set(ENABLE_PCH OFF)
    set(GLSLANG_TESTS OFF)
    set(GLSLANG_ENABLE_INSTALL OFF)
    set(BUILD_EXTERNAL OFF)
    add_subdirectory(${LIBSRC}/glslang ${CMAKE_CURRENT_BINARY_DIR}/glslang EXCLUDE_FROM_ALL)
    set(SPIRV_CROSS_CLI OFF)
    set(SPIRV_CROSS_ENABLE_TESTS OFF)
    set(SPIRV_CROSS_SHARED OFF)
    set(SPIRV_CROSS_STATIC ON)
    set(SPIRV_CROSS_ENABLE_HLSL OFF)
    set(SPIRV_CROSS_ENABLE_CPP OFF)
    set(SPIRV_CROSS_ENABLE_REFLECT OFF)
    set(SPIRV_CROSS_ENABLE_UTIL OFF)
    set(SPIRV_CROSS_ENABLE_MSL ON)
    set(SPIRV_CROSS_ENABLE_GLSL ON)
    set(SPIRV_CROSS_ENABLE_C_API ON)
    set(SPIRV_CROSS_SKIP_INSTALL ON)
    add_subdirectory(${LIBSRC}/SPIRV-Cross ${CMAKE_CURRENT_BINARY_DIR}/SPIRV-Cross EXCLUDE_FROM_ALL)
  endfunction()
  zh_add_shader_compilers()

  # SDL_shadercross is one C file, built here rather than through its own CMakeLists, whose vendored
  # mode insists on DXC's source even with DXC off.  No SDL_SHADERCROSS_DXC: HLSL never reaches it,
  # only the SPIR-V glslang wrote.
  add_library(sdl3_shadercross STATIC ${LIBSRC}/SDL_shadercross/src/SDL_shadercross.c)
  target_include_directories(sdl3_shadercross PUBLIC ${LIBSRC}/SDL_shadercross/include)
  target_link_libraries(sdl3_shadercross PUBLIC SDL3::SDL3-static PRIVATE spirv-cross-c)

  # HLSL to SPIR-V to what a device takes, behind the one function that names the HLSL front end.
  add_library(sdl3shadercompile STATIC ${WW}/WW3D2/sdl3shadercompile.cpp)
  target_include_directories(sdl3shadercompile PUBLIC ${WW}/WW3D2)
  target_link_libraries(sdl3shadercompile PUBLIC SDL3::SDL3-static
    PRIVATE sdl3_shadercross glslang::glslang glslang::glslang-default-resource-limits)

  # ---- Text off Windows (decision 6, D6) -----------------------------------------------------------
  # FreeType 2.14.3, fetched and pinned by vendor.sh, with none of its optional dependencies: the game's
  # fonts are plain TrueType files, and zlib, bzip2, PNG, HarfBuzz and Brotli only matter to compressed,
  # colour or shaped fonts.  Its gzip module uses its own zlib copy.  EXCLUDE_FROM_ALL, like the shader
  # compilers: what links it is what builds it.
  function(zh_add_freetype)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(BUILD_SHARED_LIBS OFF)
    foreach(_ft_dep ZLIB BZIP2 PNG HARFBUZZ BROTLI)
      set(FT_DISABLE_${_ft_dep} ON)
    endforeach()
    set(SKIP_INSTALL_ALL ON)
    add_subdirectory(${LIBSRC}/freetype ${CMAKE_CURRENT_BINARY_DIR}/freetype EXCLUDE_FROM_ALL)
  endfunction()
  zh_add_freetype()

  # What FontCharsClass asks GDI for, from FreeType: the fonts, their metrics, the glyphs.  Its own
  # library, outside ww3d2's list, so it builds and is tested before WW3D2 does off Windows (D6a).
  # Linux finds Liberation through fontconfig; macOS reads fixed files and needs nothing more.
  add_library(glyphrasteriser STATIC ${WW}/WW3D2/glyphrasteriser.cpp)
  target_include_directories(glyphrasteriser PUBLIC ${WW}/WW3D2)
  target_link_libraries(glyphrasteriser PRIVATE freetype)
  if(ZH_PLATFORM_LINUX)
    find_package(Fontconfig REQUIRED)
    target_link_libraries(glyphrasteriser PRIVATE Fontconfig::Fontconfig)
  endif()

  # The Miles 6.5 surface on miniaudio (C4): the same library name as the XAudio2 one on Windows, so
  # that gameenginedevice, once it builds off Windows, links milesaudio on either and
  # MilesAudioManager.cpp compiles unchanged against it.  Today only the tests below link it here.
  add_library(milesaudio STATIC ${WW}/Miles6/miniaudio/miles_miniaudio.cpp)
  target_include_directories(milesaudio PRIVATE ${WW}/Miles6/include ${WW}/WWLib)
  target_link_libraries(milesaudio PRIVATE miniaudio wwlib PUBLIC Threads::Threads)

  # What SDL3's GPU API can do on this machine: the evidence D2 and D5 need before the backend
  # interface is designed.  Needs a GPU and a display, so it is built but not registered with ctest.
  add_executable(sdl_gpu_probe ${CMAKE_CURRENT_SOURCE_DIR}/Tests/sdl_gpu_probe.cpp)
  target_link_libraries(sdl_gpu_probe PRIVATE SDL3::SDL3-static)

  # The D-spike: one real model and its DDS textures, read from the game's archives, drawn through
  # SDL3's GPU API.  Parses W3D with WWLib's chunkio and w3d_file.h; WW3D2 itself is not built.  Needs
  # a GPU and the game's data, so it is built but not registered with ctest.  Its shaders are
  # generated ahead of time by Tests/w3d_view/shaders/build_shaders.sh and checked in, so building it
  # needs no shader compiler.
  add_executable(w3d_view
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/w3d_view.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/w3d_model.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/dds_image.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view/big_archive.cpp)
  target_include_directories(w3d_view PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_view
    ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
    ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug)
  # --shaders generated draws with the game's own generated programs, compiled as the SDL3 backend
  # will compile them (D3).
  target_link_libraries(w3d_view PRIVATE SDL3::SDL3-static wwmath wwlib wwsaveload wwdebug
    ffvertex engineshader sdl3shadercompile)
endif()

# d3d8to9 is no longer built.  It was a d3d8.dll next to generals.exe that translated
# every D3D8 call to Direct3D 9, which is how the renderer ran on a modern runtime
# without touching the ~5600 D3D8 call sites.  The renderer creates its own
# IDirect3DDevice9 now (RENDERER-ROADMAP.md phase 1), so nothing loads that dll.  The
# vendored source stays in Libraries/Source/d3d8to9: WW3D2/d3d8shadertranslate.cpp is
# lifted from it, and it is the reference for how a D3D8 call was being answered.

# ===========================================================================
# Phase 3 - GameEngine.  ~580 files of game logic; no DX8, no device code.
# ===========================================================================
set(GE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngine)

# The source tree is a frozen 2003 snapshot, so a glob cannot go stale the way
# it would in a live project - and 580 explicit paths would drown this file.
file(GLOB_RECURSE GAMEENGINE_SOURCES ${GE}/Source/*.cpp)
# On disk but absent from GameEngine.dsp, i.e. never in the shipping build:
# dead experiments (SimplePlayer, urlLaunch, VideoStream), an older copy of
# DrawableManager that GameClient/Drawable/ supersedes, and GameSpeech, whose
# hash uses std::hash<const char*> on strings that outlive nothing.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/Common/Audio/GameSpeech.cpp
  ${GE}/Source/Common/Audio/simpleplayer.cpp
  ${GE}/Source/Common/Audio/urllaunch.cpp
  ${GE}/Source/GameClient/DrawableManager.cpp
  ${GE}/Source/GameClient/VideoStream.cpp)
# GameNetwork/GameSpy.h and the two .cpp files that owned it were stripped from
# the release, and these two are all that include it.  Multiplayer is out of
# scope (PLAN.md), so they stay out until the exe link says otherwise.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyChat.cpp
  ${GE}/Source/GameNetwork/GameSpyGP.cpp)
# EA's own header says "#error this file is obsolete", and this .cpp is its only
# includer - the live code went to GameNetwork/GameSpy/ instead.
list(REMOVE_ITEM GAMEENGINE_SOURCES
  ${GE}/Source/GameNetwork/GameSpyGameInfo.cpp)
# Platform bodies of one interface: each side's file is left out of the other's build, so Windows
# gets exactly the sources it had.  B5, bucket (c).
#  - StackDump.cpp: dbghelp symbol lookup and a structured-exception context walk, Windows only.
#    StackDumpPosix.cpp is the same Common/StackDump.h interface from backtrace() and dladdr();
#    crash reporting proper is C5's.
#  - WebBrowser.cpp: an ATL/COM wrapper around Internet Explorer for the dead WOL service's pages.
#    Nothing ever creates TheWebBrowser, on Windows either (Win32GameEngine::createWebBrowser has
#    no caller), and every caller - INIWebpageURL, WOLLadderScreen, WOLLoginMenu - tests it for NULL
#    first.  WebBrowserPosix.cpp is the same interface without COM, TheWebBrowser NULL.
#  - DownloadManager.cpp: patch downloads over wwdownload (FTP over winsock; wwdownload itself is a
#    Windows-only target above) from a dead service.  Only DownloadMenuInit creates the manager,
#    and only on Windows; MainMenu and MainMenuUtils test TheDownloadManager for NULL first.
#    DownloadManagerPosix.cpp defines it NULL.  DownloadMenu.cpp itself is built everywhere, because
#    .wnd files bind its callbacks by name through FunctionLexicon, which must be the same on
#    every platform.
#  - MapObjectRenderPosix.cpp: MapObject's three RenderObjClass members.  Windows has them beside the
#    W3D terrain code in gameenginedevice (WorldHeightMap.cpp); the data half is Common/MapObject.cpp
#    on both.  B6.
#  - WWDownloadRegistryPosix.cpp: WWDownload's registry functions and FormatURLFromRegistry over
#    Registry.ini.  On Windows they come from wwdownload itself.  B6.
#  - IMEManager.cpp: the IMM32 input method editor (Chinese, Japanese and Korean text entry).  Its
#    only exports are TheIMEManager and CreateIMEManagerInterface; GameClient::init handles a NULL
#    manager and every other caller tests for one, and no test calls it.  IMEManagerPosix.cpp is the
#    manager off Windows, over the device's text events (C3): every typed character reaches a text
#    field through the IME manager, on Windows too.  (IMECandidate.cpp, whose window callbacks .wnd files
#    name through FunctionLexicon, is a different file and is built everywhere.)
if(ZH_PLATFORM_WINDOWS)
  list(REMOVE_ITEM GAMEENGINE_SOURCES
    ${GE}/Source/Common/System/StackDumpPosix.cpp
    ${GE}/Source/GameNetwork/WOLBrowser/WebBrowserPosix.cpp
    ${GE}/Source/GameNetwork/DownloadManagerPosix.cpp
    ${GE}/Source/GameClient/GUI/IMEManagerPosix.cpp
    ${GE}/Source/Common/MapObjectRenderPosix.cpp
    ${GE}/Source/Common/System/WWDownloadRegistryPosix.cpp)
else()
  list(REMOVE_ITEM GAMEENGINE_SOURCES
    ${GE}/Source/Common/System/StackDump.cpp
    ${GE}/Source/GameNetwork/WOLBrowser/WebBrowser.cpp
    ${GE}/Source/GameNetwork/DownloadManager.cpp
    ${GE}/Source/GameClient/GUI/IMEManager.cpp)
endif()

# ZH_NOT_YET_PORTABLE: defined on macOS, kept out of the default build there until B1-B6 land.
# The link list below is left exactly as it is on purpose - B6 owns it.
add_library(gameengine STATIC ${ZH_NOT_YET_PORTABLE} ${GAMEENGINE_SOURCES})
target_include_directories(gameengine PUBLIC
  ${GE}/Include ${GE}/Include/Precompiled
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW} ${WW}/WWLib
  # The game includes "gamespy/peer/peer.h", the SDK's own headers include each
  # other as "gscommon.h", so both levels have to be on the path.
  ${LIBSRC}/GameSpy/include ${LIBSRC}/GameSpy/include/gamespy ${COMP}
  # WW3D2 is here for d3dx9math.h alone: BezierSegment carries D3DXMATRIX, and
  # DumbProjectileBehavior steers a shell with it.  dinput.h now comes out of the
  # Windows SDK like d3d9.h does.
  ${WW}/WW3D2
  # nanosvg's two headers, compiled into HtmlOverlay.cpp alone: the SVG pictures a page names.
  ${LIBSRC}/nanosvg)
# _RELEASE/_DEBUG pick the game's own build flavour; IG_DEBUG_STACKTRACE and
# Z_PREFIX are what GameEngine.dsp sets.
target_compile_definitions(gameengine PUBLIC
  IG_DEBUG_STACKTRACE Z_PREFIX
  $<$<CONFIG:Debug>:_DEBUG> $<$<NOT:$<CONFIG:Debug>>:_RELEASE>)
# C++17 for the std::hash_map -> unordered_map alias and string_view hashing in
# STLTypedefs.h.
set_target_properties(gameengine PROPERTIES CXX_STANDARD 17)

# setFPMode's POSIX branch must leave floating point as Windows' default MXCSR does: round to nearest,
# denormals kept, NaN payloads propagated.  Built from FPUControl.cpp alone - it lives in its own
# translation unit for exactly this - with the engine's includes and definitions, so it runs before
# gameengine links.  Armed: it first sets the state setFPMode must undo and requires every check to
# fail there.  POSIX only for now; nothing here has run it on Windows.  See Tests/test_fpucontrol.cpp.
if(ZH_PLATFORM_POSIX)
  add_executable(fpucontrol_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_fpucontrol.cpp
                                      ${GE}/Source/GameLogic/System/FPUControl.cpp
                                      ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
  target_include_directories(fpucontrol_selfcheck PRIVATE
    $<TARGET_PROPERTY:gameengine,INTERFACE_INCLUDE_DIRECTORIES>)
  target_compile_definitions(fpucontrol_selfcheck PRIVATE
    $<TARGET_PROPERTY:gameengine,INTERFACE_COMPILE_DEFINITIONS>
    ZH_EAGER_VTABLE_STUB_STRINGS)		# it links no AsciiString.cpp or UnicodeString.cpp
  set_target_properties(fpucontrol_selfcheck PROPERTIES CXX_STANDARD 17)
endif()
# Two names on this line are Windows-only, and expand to nothing elsewhere - in the same positions,
# so the Windows list is the same items in the same order it always was.  B6's first two drops:
#  - debuglib: no live GameEngine code calls it (every Debug::/DLOG/DASSERT hit is commented out).
#    It still reaches Windows through profile, which does call it.
#  - dinput8: GameEngine uses the 107 DIK_ key-code constants from <dinput.h> and no DirectInput
#    function.  It is load-bearing on Windows all the same: Win32DIKeyboard.cpp and Win32DIMouse.cpp
#    in gameenginedevice call DirectInput8Create and get dinput8 only through this PUBLIC line.
#
# And seven more that exist only on Windows, whose off-Windows parts gameengine gets elsewhere (B6).
# Off Windows none is a target, so each reached the linker as -l<name> and a POSIX link failed on it:
#  - ww3d2: gameengine's references are DX8Wrapper_PreserveFPU and DX8Wrapper_IsWindowed, which
#    PosixDevice defines, and d3dx9math.h's maths, which d3dxportable.h makes header-only (B17).
#  - wwdownload: its registry API and FormatURLFromRegistry, gameengine's own off Windows (B6).
#  - eabrowserdispatch, profile, wininet, imagehlp, imm32: Windows-only code, left out of the POSIX
#    build by B5 (WebBrowserPosix, StackDumpPosix, IMEManagerPosix, ChromaKeyboard's WinINet half).
# A link of gameengine with PosixDevice and the libraries below leaves no symbol any of them defines.
if(ZH_PLATFORM_WINDOWS)
  set(ZH_GAMEENGINE_DEBUGLIB debuglib)
  set(ZH_GAMEENGINE_DINPUT8 dinput8)
  set(ZH_GAMEENGINE_WW3D2 ww3d2)
  set(ZH_GAMEENGINE_WWDOWNLOAD wwdownload)
  set(ZH_GAMEENGINE_EABROWSERDISPATCH eabrowserdispatch)
  set(ZH_GAMEENGINE_PROFILE profile)
  set(ZH_GAMEENGINE_WIN32_SDK_LIBS wininet imagehlp imm32)
endif()
target_link_libraries(gameengine PUBLIC
  ${ZH_GAMEENGINE_WW3D2} wwlib wwmath wwsaveload wwdebug wwutil compression ${ZH_GAMEENGINE_DEBUGLIB} ${ZH_GAMEENGINE_WWDOWNLOAD}
  benchmark ${ZH_GAMEENGINE_EABROWSERDISPATCH} ${ZH_GAMEENGINE_PROFILE} gamespy litehtml
  ${ZH_GAMEENGINE_DINPUT8} ${ZH_GAMEENGINE_WIN32_SDK_LIBS})

# B1's gate: the bytes a UnicodeString puts into the replay and network CRC, before and after
# WideChar becomes char16_t.  Built from the REAL Xfer, XferCRC, UnicodeString, AsciiString and
# WideCharFns sources with gameengine's own include paths and definitions, and nothing else of
# gameengine, so it runs on every platform today rather than when gameengine links.  The
# -fshort-wchar twin gives wchar_t MSVC's two unsigned bytes, which is the Windows model on a
# Mac or Linux box; see Tests/test_widechar_crc_gate.cpp for what the pair proves and does not.
# The real sources both of B1's tests are built from, and the one stub file they share.
set(ZH_WIDECHAR_TEST_SUPPORT
  ${GE}/Source/Common/System/Xfer.cpp ${GE}/Source/Common/System/XferCRC.cpp
  ${GE}/Source/Common/System/UnicodeString.cpp ${GE}/Source/Common/System/AsciiString.cpp
  ${GE}/Source/Common/System/WideCharFns.cpp ${CMAKE_CURRENT_SOURCE_DIR}/Tests/widechar_test_stubs.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/Tests/gcc_eager_vtable_stubs.cpp)
# XferCRC.cpp, MemoryInit.cpp and their kind open files through WWLib's zh_fopen, which off Windows is
# a function in wwlib (C1 (d)); on Windows it is fopen, inline, and needs nothing linked.
if(ZH_PLATFORM_POSIX)
  set(ZH_ZHIO_LIBS wwlib wwdebug)
endif()
function(zh_widechar_gate name)
  add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_crc_gate.cpp ${ZH_WIDECHAR_TEST_SUPPORT})
  target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(${name} PROPERTIES CXX_STANDARD 17)
  target_link_libraries(${name} PRIVATE ${ZH_ZHIO_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endfunction()
zh_widechar_gate(widechar_crc_gate)
# The wide-format funnel against MSVC's legacy wide printf: %s wide, %S narrow.  Off Windows it
# checks the hand-written formatter; on Windows the same expected strings check the real
# _vsnwprintf.  See Tests/test_widechar_format.cpp.
add_executable(widechar_format_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_format.cpp
  ${ZH_WIDECHAR_TEST_SUPPORT})
target_include_directories(widechar_format_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(widechar_format_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(widechar_format_selfcheck PROPERTIES CXX_STANDARD 17)
target_link_libraries(widechar_format_selfcheck PRIVATE ${ZH_ZHIO_LIBS})
add_test(NAME widechar_format_selfcheck COMMAND widechar_format_selfcheck)
# The game's own .csf through the real GameText.cpp, RAMFile, File and memory manager: every string
# against an independent reading, some against their English text, and a golden hash for a Windows
# run to diff.  Needs ZH_GAME_DATA; without it the test is Skipped (77), not Passed.  Stubs are in
# the test file.  See Tests/test_gametext_csf.cpp.
add_executable(gametext_csf ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gametext_csf.cpp
  ${GE}/Source/GameClient/GameText.cpp ${GE}/Source/Common/System/RAMFile.cpp
  ${GE}/Source/Common/System/File.cpp ${GE}/Source/Common/System/GameMemory.cpp
  ${GE}/Source/Common/System/MemoryInit.cpp ${GE}/Source/Common/System/UnicodeString.cpp
  ${GE}/Source/Common/System/AsciiString.cpp ${GE}/Source/Common/System/WideCharFns.cpp)
target_include_directories(gametext_csf PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(gametext_csf PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(gametext_csf PROPERTIES CXX_STANDARD 17)
target_link_libraries(gametext_csf PRIVATE ${ZH_ZHIO_LIBS})
add_test(NAME gametext_csf COMMAND gametext_csf)
# Strings built by a static constructor before anything starts the memory manager, as file-scope
# LogClass objects build them: the real strings and memory manager, each string type first in one of
# the two.  See Tests/test_premain_strings.cpp.
foreach(name test_premain_strings test_premain_unicode)
  add_executable(${name} ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_premain_strings.cpp
    ${GE}/Source/Common/System/GameMemory.cpp ${GE}/Source/Common/System/MemoryInit.cpp
    ${GE}/Source/Common/System/UnicodeString.cpp ${GE}/Source/Common/System/AsciiString.cpp
    ${GE}/Source/Common/System/WideCharFns.cpp)
  target_include_directories(${name} PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(${name} PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(${name} PROPERTIES CXX_STANDARD 17)
  target_link_libraries(${name} PRIVATE ${ZH_ZHIO_LIBS})
  add_test(NAME ${name} COMMAND ${name})
endforeach()
target_compile_definitions(test_premain_unicode PRIVATE ZH_PREMAIN_UNICODE_FIRST)
set_tests_properties(gametext_csf PROPERTIES SKIP_RETURN_CODE 77)
if(ZH_GAME_DATA)
  set_tests_properties(gametext_csf PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()

# The replay header's wide strings as byte calls, sharing a stream with fwrite and fread (C1, PR (g)).
# See Tests/test_widechar_file.cpp.
add_executable(widechar_file_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_widechar_file.cpp
  ${ZH_WIDECHAR_TEST_SUPPORT})
target_include_directories(widechar_file_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(widechar_file_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(widechar_file_selfcheck PROPERTIES CXX_STANDARD 17)
target_link_libraries(widechar_file_selfcheck PRIVATE test_harness ${ZH_ZHIO_LIBS})
add_test(NAME widechar_file_selfcheck COMMAND widechar_file_selfcheck)
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
  zh_widechar_gate(widechar_crc_gate_msvc_model)
  target_compile_options(widechar_crc_gate_msvc_model PRIVATE -fshort-wchar)
  # The five wcs* functions UnicodeString.cpp calls, over two-byte units as MSVC's CRT has them:
  # this C library was built with four-byte wchar_t and would overrun the heap.
  target_sources(widechar_crc_gate_msvc_model PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Tests/widechar_gate_msvc_crt.cpp)
endif()

# ===========================================================================
# PosixDevice - the POSIX platform layer beside Win32Device (C1), a file at a time
# ===========================================================================
# The Win32Device classes' POSIX counterparts, for macOS and Linux alike; what only macOS has goes
# in MacDevice.  Out of the default build on macOS for as long as gameengine is (ZH_NOT_YET_PORTABLE),
# so drive it with --target.
if(ZH_PLATFORM_POSIX)
  file(GLOB_RECURSE POSIXDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/*.cpp)
  # PosixDevice/Render is the Direct3D 9-shaped device (decision 7), a library of its own below: WW3D2
  # links it the way it links d3d9.lib on Windows, and posixdevice needs none of it.
  list(FILTER POSIXDEVICE_SOURCES EXCLUDE REGEX "/PosixDevice/Render/")
  # The BIG archive reader is Win32Device's own, built here as it is (C1 (f)): it has no Win32 in it but
  # ntohl and one message box, both behind #if, and one copy keeps the mount order - which archive wins
  # which path, and so the INI CRC - the same on every platform.
  list(APPEND POSIXDEVICE_SOURCES
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp)
  add_library(posixdevice STATIC ${ZH_NOT_YET_PORTABLE} ${POSIXDEVICE_SOURCES})
  target_include_directories(posixdevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
  set_target_properties(posixdevice PROPERTIES CXX_STANDARD 17)
  target_link_libraries(posixdevice PUBLIC gameengine)
  # PosixRenderHooks.cpp on its own, ahead of gameengine linking: decision 2 (B19 (c)) lives in its
  # testMinimumRequirements, checked against the shipped GameLODPresets.ini when ZH_GAME_DATA is set.
  # See Tests/test_posix_render_hooks.cpp.
  add_executable(posix_render_hooks_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_posix_render_hooks.cpp
                 ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/GameClient/PosixRenderHooks.cpp)
  target_include_directories(posix_render_hooks_selfcheck PRIVATE
    $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    ${LIBSRC}/Benchmark)
  target_compile_definitions(posix_render_hooks_selfcheck PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(posix_render_hooks_selfcheck PROPERTIES CXX_STANDARD 17)
  target_link_libraries(posix_render_hooks_selfcheck PRIVATE wwlib wwdebug benchmark)
  add_test(NAME posix_render_hooks_selfcheck COMMAND posix_render_hooks_selfcheck)
  if(ZH_GAME_DATA)
    set_tests_properties(posix_render_hooks_selfcheck PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()
endif()

# ===========================================================================
# The renderer off Windows (decision 7): WW3D2 and W3DDevice against a Direct3D 9-shaped device
# ===========================================================================
# WW3D2 and W3DDevice compile unchanged in their D3D9 calls: <d3d9.h> is Platform/PosixD3D9's, which is
# Platform/D3D9Posix.h, and the device behind it is posixd3d9.  Phase A1 builds and links them; the
# device has no resources until A2 and no draw until A3.  All of it is in the default build, the link
# probe included, so a change that breaks the renderer off Windows fails the Mac build.
if(ZH_PLATFORM_POSIX)
  set(ZH_POSIX_D3D9_INCLUDE ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include/Platform/PosixD3D9)

  # ---- d3dx9runtime: the same library name as Windows's, with d3dx9posix.cpp's bodies ---------------
  # d3dx9posix_texture.cpp is A2's (a contributor's), d3dx9posix.cpp the rest (a contributor's).
  add_library(d3dx9runtime STATIC
    ${WW}/WW3D2/d3dx9posix.cpp ${WW}/WW3D2/d3dx9posix_texture.cpp ${WW}/WW3D2/d3d8shadertranslate.cpp)
  set_target_properties(d3dx9runtime PROPERTIES CXX_STANDARD 17)
  target_include_directories(d3dx9runtime PUBLIC ${WW}/WW3D2 ${ZH_POSIX_D3D9_INCLUDE}
    # The texture helpers are CPU work over the device's own images (a contributor's PosixImageOps), so they see
    # the device's headers - privately: nothing else outside the device does.
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render)
  target_link_libraries(d3dx9runtime PRIVATE posixd3d9)

  # The matrix functions and the FVF size against independent answers, and the binding's contract;
  # the header says what it does not prove.
  add_executable(d3dx9posix_selfcheck ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_d3dx9posix.cpp)
  set_target_properties(d3dx9posix_selfcheck PROPERTIES CXX_STANDARD 17)
  target_include_directories(d3dx9posix_selfcheck PRIVATE ${WW}/WWLib ${WW}/WWDebug ${WW}/WWMath)
  target_link_libraries(d3dx9posix_selfcheck PRIVATE d3dx9runtime)
  add_test(NAME d3dx9posix_selfcheck COMMAND d3dx9posix_selfcheck)

  # ---- posixd3d9: the device, in the place d3d9.lib has on Windows -----------------------------------
  file(GLOB_RECURSE POSIXD3D9_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Render/*.cpp)
  add_library(posixd3d9 STATIC ${POSIXD3D9_SOURCES})
  set_target_properties(posixd3d9 PROPERTIES CXX_STANDARD 17)
  target_include_directories(posixd3d9 PUBLIC ${ZH_POSIX_D3D9_INCLUDE})

  # ---- ww3d2 -----------------------------------------------------------------------------------------
  add_library(ww3d2 STATIC ${ZH_WW3D2_SOURCES} ${WW}/WW3D2/dx11runtime_posix.cpp)
  set_target_properties(ww3d2 PROPERTIES CXX_STANDARD 17)
  target_include_directories(ww3d2
    PUBLIC ${WW}/WW3D2 ${ZH_POSIX_D3D9_INCLUDE}
    PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil
            ${WW}/wwshade ${WW}/WWAudio ${WW}/Miles6/include)
  target_link_libraries(ww3d2 PUBLIC posixd3d9 d3dx9runtime ffshader
    PRIVATE wwlib wwmath wwdebug wwsaveload)

  # ---- w3ddevice: GameEngineDevice's W3DDevice, gameenginedevice's renderer half ---------------------
  file(GLOB_RECURSE W3DDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/W3DDevice/*.cpp)
  # W3DMouse is a Win32Mouse, the hardware cursor through the window procedure: the platform layer's
  # input (C3), not the renderer.  W3DGameClient leaves its keyboard and mouse to the subclass off Windows.
  list(FILTER W3DDEVICE_SOURCES EXCLUDE REGEX "/W3DDevice/GameClient/W3DMouse\\.cpp$")
  # W3DWebBrowser is the embedded browser as a COM object, which only Win32GameEngine makes; GameEngine
  # has its own WebBrowser off Windows (B5).
  list(FILTER W3DDEVICE_SOURCES EXCLUDE REGEX "/W3DDevice/GameClient/W3DWebBrowser\\.cpp$")
  add_library(w3ddevice STATIC ${W3DDEVICE_SOURCES})
  set_target_properties(w3ddevice PROPERTIES CXX_STANDARD 17)
  target_include_directories(w3ddevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Stubs ${CMAKE_CURRENT_SOURCE_DIR}/Main
            ${WW}/Miles6/include ${WW}/Bink/include)
  target_link_libraries(w3ddevice PUBLIC gameengine ww3d2)

  # A1's link checkpoint: all of w3ddevice and ww3d2, whole archive, against what the POSIX game links.
  add_executable(w3d_link_probe ${CMAKE_CURRENT_SOURCE_DIR}/Tests/w3d_link_probe.cpp)
  set_target_properties(w3d_link_probe PROPERTIES CXX_STANDARD 17)
  target_link_libraries(w3d_link_probe PRIVATE
    "$<LINK_LIBRARY:WHOLE_ARCHIVE,w3ddevice,ww3d2>" posixdevice gameengine)
endif()

# ===========================================================================
# Phase 4 - GameEngineDevice + Main -> generals.exe
# ===========================================================================
# All of Phase 4 is Windows: Win32Device is the platform layer, the audio and video backends are
# XAudio2 and a Windows FFmpeg build linked from .lib files, and generals.exe itself is a WIN32
# subsystem binary started from WinMain.  C1 through C4 build the macOS equivalents, one at a
# time, and this guard is what they open up.  The block is left unindented so that the diff which
# added the guard is the guard.
# ---- The version headers, for generals on every platform ---------------------
# BuildVersion.h and GeneratedVersion.h are normally written by Tools/
# buildVersionUpdate and Tools/versionUpdate as a pre-build step, from a build
# counter in the source tree.  Both are three-line headers, so write them at
# configure time instead - it keeps two more VC6 tools out of the port, and
# Windows wants UAC elevation for exe names containing "version" anyway.
set(GENERATED ${CMAKE_CURRENT_BINARY_DIR}/generated)
file(WRITE ${GENERATED}/BuildVersion.h
"// Generated by CMake, standing in for Tools/buildVersionUpdate.\n"
"#define VERSION_STRING \"v2.1.0\"\n"
"#define VERSION_MAJOR 2\n"
"#define VERSION_MINOR 1\n"
"#define VERSION_BUILDNUM 0\n")
file(WRITE ${GENERATED}/GeneratedVersion.h
"// Generated by CMake, standing in for Tools/versionUpdate.\n"
"#define LOCAL_BUILD_STRING \"0\"\n"
"#define VERSION_LOCALBUILDNUM 0\n"
"#define VERSION_BUILDUSER \"cmake\"\n"
"#define VERSION_BUILDLOC \"${CMAKE_HOST_SYSTEM_NAME}\"\n")

if(ZH_PLATFORM_WINDOWS)
set(GED ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice)
set(STUBS ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)

file(GLOB_RECURSE GAMEENGINEDEVICE_SOURCES ${GED}/Source/*.cpp)
# On disk but absent from GameEngineDevice.dsp: the DirectInput mouse (the
# shipping build uses the Win32 message mouse instead).
list(REMOVE_ITEM GAMEENGINEDEVICE_SOURCES
  ${GED}/Source/Win32Device/GameClient/Win32DIMouse.cpp)
# PosixDevice is the POSIX platform layer (C1), built below as posixdevice, never on Windows.
list(FILTER GAMEENGINEDEVICE_SOURCES EXCLUDE REGEX "/Source/PosixDevice/")
# Sound and video are the game's own.  Miles and Bink were binary-only, and the DLLs the retail
# game ships are 32-bit, so there is nothing for a 64-bit exe to bind to: what it links instead is
# those two APIs written over XAudio2 and FFmpeg.  FFmpeg decodes binkvideo and binkaudio natively,
# so the .bik files on disk play as they are, and swscale converts each frame into whatever surface
# format the video player asks for.  MilesAudioManager.cpp and BinkVideoPlayer.cpp are unchanged and
# still call AIL_* and Bink*; the reconstructed headers in Miles6/include and Bink/include are still
# the declarations.  miles_smoke and bink_smoke are the checks.
set(FFMPEG_DIR ${LIBSRC}/FFmpeg/dist)
if(NOT EXISTS ${FFMPEG_DIR}/include)
  message(FATAL_ERROR
    "FFmpeg is missing from Libraries/Source/FFmpeg/dist - see the vendored-libs rule.")
endif()

set(FFMPEG_CORE
  ${FFMPEG_DIR}/lib/avformat.lib ${FFMPEG_DIR}/lib/avcodec.lib ${FFMPEG_DIR}/lib/avutil.lib
  ${FFMPEG_DIR}/lib/swresample.lib)

add_library(binkvideo STATIC ${WW}/Bink/ffmpeg/bink_ffmpeg.cpp)
target_include_directories(binkvideo PRIVATE ${WW}/Bink/include ${FFMPEG_DIR}/include)
target_link_libraries(binkvideo PRIVATE ${FFMPEG_CORE} ${FFMPEG_DIR}/lib/swscale.lib xaudio2)

add_library(milesaudio STATIC ${WW}/Miles6/xaudio2/miles_xaudio2.cpp)
target_include_directories(milesaudio PRIVATE ${WW}/Miles6/include ${FFMPEG_DIR}/include)
target_link_libraries(milesaudio PRIVATE ${FFMPEG_CORE} xaudio2)

add_library(gameenginedevice STATIC ${GAMEENGINEDEVICE_SOURCES})

target_include_directories(gameenginedevice PUBLIC
  ${GED}/Include ${STUBS} ${CMAKE_CURRENT_SOURCE_DIR}/Main
  ${WW}/Miles6/include ${WW}/Bink/include)
set_target_properties(gameenginedevice PROPERTIES CXX_STANDARD 17)
target_link_libraries(gameenginedevice PUBLIC gameengine ww3d2 milesaudio binkvideo)

# ---- generals.exe ---------------------------------------------------------
add_executable(generals WIN32 ${CMAKE_CURRENT_SOURCE_DIR}/Main/WinMain.cpp
                             ${CMAKE_CURRENT_SOURCE_DIR}/Main/RTS.RC)
target_include_directories(generals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main ${GENERATED})
# A .map turns the "fault offset" in the Windows Application Error event into a
# function name without a debugger: find the last entry whose RVA is <= the offset.
target_link_options(generals PRIVATE /MAP)
# /LARGEADDRESSAWARE used to be here, asking 64-bit Windows for the whole 4GB a 32-bit process can
# address.  An eight-player Merciless match on Twilight Flame was already at 978MB of it.  The
# ceiling it was working against does not exist in this build.
set_target_properties(generals PROPERTIES CXX_STANDARD 17)
target_link_libraries(generals PRIVATE gameenginedevice
  winmm dsound wsock32 ole32 oleaut32 uuid)
# The game reads its data relative to the working directory, so it has to sit
# beside Run/'s .big files to find anything (Phase 5).
add_custom_command(TARGET generals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The PDB has to sit next to the exe: SymInitialize searches the exe's directory,
  # and without it the crash log's stack is addresses with no names.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          $<TARGET_PDB_FILE:generals> ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The fork's INI files: the key bindings and their labels, and the detonation
  # light loaded after EA's FXList.ini.  Run/ is gitignored, so these used to be
  # hand-made files that silently went stale - the tracked copies under Code/Data
  # are the masters and this puts them where the game reads them.  A loose file
  # beats every .big and the game never writes one back, so overwriting them on
  # each build is safe.  The whole directory, so a new master is copied without a
  # line here: anything in it lands in the multiplayer INI CRC, and package.bat
  # ships the same directory and refuses a Run/ that disagrees with it.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/INI
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/INI
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Patch.str
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/
  # The skirmish AI's scripts with the USA Hard opening rewritten by Tools/ScbTool/opening.py.  The
  # file is in the multiplayer checksum (GlobalData.cpp), so it ships with the exe like the INIs.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Scripts
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Scripts
  # The translations the Language option lays over the English strings, one directory per
  # language.  Strings are not in the multiplayer INI checksum: two players can read one match in
  # two languages.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Turkish
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Data/Turkish
  # The startup splash.  WinMain loads it from a loose file next to the exe, so
  # the fork's own artwork is a tracked master here rather than a hand-made file
  # in gitignored Run/.
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Install_Final.bmp
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/
  # The three command bar plates per side and the smoke signals' ground marks.  A loose
  # texture beats every .big, so these land in Run/Art/Textures where the W3D file
  # system looks first.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Art/Textures
  # The fork's window layouts, generated from the shipped ones by Tools/*.py and
  # tracked here.  They live outside Data/ in the archive (Window/Menus/...), and
  # a loose file wins over WindowZH.big.  Layouts are not in the multiplayer INI
  # checksum, so an out of date copy costs a wrong looking screen, not a refused
  # join.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Window
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Window
  # -scenario reads these. They are a measuring tool rather than game data: outside Data/ so they
  # cannot collide with an archive path, and outside the multiplayer INI checksum because nothing
  # loads them with &xferCRC. They are not in package.bat for the same reason - a player has no use
  # for one, and a scenario run cannot be replayed anyway.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Scenarios
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Scenarios
  # -cinema reads these shot lists. Client-side only, never checksummed, and a tool for making
  # footage rather than anything a player needs, so they stay out of package.bat too.
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          ${CMAKE_CURRENT_SOURCE_DIR}/Data/Cinema
          ${CMAKE_CURRENT_SOURCE_DIR}/../Run/Cinema)

# The audio and video backends decode with FFmpeg, so its DLLs have to sit next to the exe.  These
# are the five the two backends link against; avdevice and avfilter are not used.
set(FFMPEG_RUNTIME
  ${FFMPEG_DIR}/bin/avcodec-62.dll ${FFMPEG_DIR}/bin/avformat-62.dll ${FFMPEG_DIR}/bin/avutil-60.dll
  ${FFMPEG_DIR}/bin/swresample-6.dll ${FFMPEG_DIR}/bin/swscale-9.dll)
add_custom_command(TARGET generals POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${FFMPEG_RUNTIME} ${CMAKE_CURRENT_SOURCE_DIR}/../Run/)
endif()  # ZH_PLATFORM_WINDOWS - GameEngineDevice, the backends and generals.exe

# ===========================================================================
# The game off Windows (C2): SdlDevice and the POSIX entry point
# ===========================================================================
# SdlGameEngine is PosixGameEngine (C1) with SDL3's window and events under it; Main/PosixMain.cpp is
# WinMain's start-up sequence without the Windows.  Until T1 and D4 the engine stops at the first
# factory only a renderer can answer, and says so (SdlGameEngine.h).
if(ZH_PLATFORM_POSIX)
  # Windows' audio manager, unchanged but for its guarded non-AIL remainder, over the Miles surface on
  # miniaudio (C4's upper half).  Its own target until A1 builds gameenginedevice off Windows, whose
  # source glob already holds this file; then this target goes, so it is never compiled twice.
  add_library(milesaudiomanager STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/MilesAudioDevice/MilesAudioManager.cpp)
  target_include_directories(milesaudiomanager PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    ${WW}/Miles6/include)
  set_target_properties(milesaudiomanager PROPERTIES CXX_STANDARD 17)
  target_link_libraries(milesaudiomanager PUBLIC gameengine milesaudio)

  file(GLOB_RECURSE SDLDEVICE_SOURCES ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/SdlDevice/*.cpp)
  add_library(sdldevice STATIC ${SDLDEVICE_SOURCES})
  target_include_directories(sdldevice PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Stubs)
  set_target_properties(sdldevice PROPERTIES CXX_STANDARD 17)
  target_link_libraries(sdldevice PUBLIC posixdevice SDL3::SDL3-static milesaudiomanager)

  add_executable(generals ${CMAKE_CURRENT_SOURCE_DIR}/Main/PosixMain.cpp)
  target_include_directories(generals PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/Main ${GENERATED})
  set_target_properties(generals PROPERTIES CXX_STANDARD 17)
  target_link_libraries(generals PRIVATE sdldevice)
  # C2's SDL side of Monitors.h and MessageBoxWrapper, on SDL's offscreen video driver.
  add_executable(test_sdl_platform ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_platform.cpp)
  target_include_directories(test_sdl_platform PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_sdl_platform PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(test_sdl_platform PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_sdl_platform PRIVATE sdldevice test_harness)
  add_test(NAME test_sdl_platform COMMAND test_sdl_platform)

  # C4's upper half: Windows' MilesAudioManager over the Miles surface on miniaudio's null backend, fed
  # the install's own INIs and sounds through a read-only farm of its archives (rule 9), heard through
  # the mix tap.  Needs ZH_GAME_DATA; Skipped (77) without it.
  add_executable(test_milesaudiomanager ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_milesaudiomanager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gameengine_stubs.cpp)
  target_include_directories(test_milesaudiomanager PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_milesaudiomanager PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(test_milesaudiomanager PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_milesaudiomanager PRIVATE sdldevice test_harness)
  add_test(NAME test_milesaudiomanager COMMAND test_milesaudiomanager)
  set_tests_properties(test_milesaudiomanager PROPERTIES SKIP_RETURN_CODE 77)
  if(ZH_GAME_DATA)
    set_tests_properties(test_milesaudiomanager PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null;ZH_DATA_DIR=${ZH_GAME_DATA}")
  else()
    set_tests_properties(test_milesaudiomanager PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")
  endif()

  # C3a: input on SDL3's offscreen driver - the scancode table against DIKeyCodes.h, keys into the
  # engine's key state, text into the IME manager, the mouse, and the install's .ANI cursors (read only,
  # ZH_DATA_DIR; said when absent).
  # test_gameengine_stubs.cpp: the names the executable supplies (gAppPrefix, the .csf/.str paths).
  add_executable(test_sdl_input ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_sdl_input.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Tests/test_gameengine_stubs.cpp)
  target_include_directories(test_sdl_input PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
  target_compile_definitions(test_sdl_input PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>
    DIKEYCODES_H="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine/Include/GameClient/DIKeyCodes.h")
  set_target_properties(test_sdl_input PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_sdl_input PRIVATE sdldevice test_harness)
  add_test(NAME test_sdl_input COMMAND test_sdl_input)
  if(ZH_GAME_DATA)
    set_tests_properties(test_sdl_input PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
  endif()

  # The entry sequence without game data: see the script.
  add_test(NAME generals_smoke
           COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/run_generals_smoke.sh $<TARGET_FILE:generals>)
endif()

# ===========================================================================
# Tests.  Run with:  ctest --test-dir build -C Release --output-on-failure
# ===========================================================================
enable_testing()

set(TESTS ${CMAKE_CURRENT_SOURCE_DIR}/Tests)

# Shared runner for the micro-harness in Tests/test_harness.h; every test
# binary links it instead of writing its own main().
add_library(test_harness STATIC ${TESTS}/test_main.cpp)
target_include_directories(test_harness PUBLIC ${TESTS})
# Off Windows, every binary that links the harness - its own main() or the runner's - refuses to start
# without ZH_USER_DATA_DIR, which otherwise leaves the engine to use this user's real data directory.
# An INTERFACE source so it is compiled into each executable, where a static library's object would be
# dropped by any binary that does not reference it.  ctest gives each test its folder at the end of
# this file.
if(ZH_PLATFORM_POSIX)
  target_sources(test_harness INTERFACE ${TESTS}/test_user_data_guard.cpp)
endif()

# add_lib_test(<name> [PENDING_MACOS] <libs...>) -> Tests/<name>.cpp, built and registered.
#
# PENDING_MACOS says the test's libraries are ones the macOS port has not brought up yet.  On a Mac
# the executable is still defined - tasks B1-B6 need something to compile - but it stays out of the
# default build and out of ctest, because a test that cannot be linked is not a failing test, it is
# a test that does not exist here yet.  On Windows the keyword changes nothing at all.
function(add_lib_test name)
  set(libs ${ARGN})
  set(exclude "")
  if("PENDING_MACOS" IN_LIST libs)
    list(REMOVE_ITEM libs PENDING_MACOS)
    set(exclude "${ZH_NOT_YET_PORTABLE}")
  endif()
  add_executable(${name} ${exclude} ${TESTS}/${name}.cpp)
  target_link_libraries(${name} PRIVATE test_harness ${libs})
  add_test(NAME ${name} COMMAND ${name})
  if(NOT exclude STREQUAL "")
    # PENDING_MACOS used to skip add_test() altogether, so `ctest` on macOS
    # listed 11 tests where Windows lists 16 and said nothing about the other
    # five.  An absent test reads exactly like a passing one, and "ctest is
    # green" was being taken as evidence of progress across the whole port.
    # Registering it DISABLED keeps it out of the run - the binary is
    # EXCLUDE_FROM_ALL and would not link - and puts it in the output, which is
    # where a test somebody still owes belongs.
    set_tests_properties(${name} PROPERTIES DISABLED TRUE)
    set_property(GLOBAL APPEND PROPERTY ZH_DEFERRED_TESTS ${name})
  endif()
endfunction()

add_lib_test(test_wwmath wwmath wwlib wwsaveload wwdebug)
target_include_directories(test_wwmath PRIVATE ${WW}/WWLib ${WW}/WWSaveLoad ${WW}/Wwutil)

add_lib_test(test_wwlib wwlib wwmath wwsaveload wwdebug)
target_include_directories(test_wwlib PRIVATE ${WW}/WWLib ${WW}/WWMath ${WW}/WWSaveLoad ${WW}/Wwutil)
# generals.exe is linked /LARGEADDRESSAWARE, and that flag changes what
# GlobalMemoryStatus saturates its 32-bit fields to (0xFFFFFFFF, not 0x7FFFFFFF).
# cpudetect_memory_fits_in_the_signed_int_its_callers_use only means anything in a
# binary that sees the same value the game does.
# A cl.exe linker switch, so it asks the compiler rather than the platform.
if(MSVC)
  target_link_options(test_wwlib PRIVATE /LARGEADDRESSAWARE)
endif()

add_lib_test(test_wwsaveload wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_wwsaveload PRIVATE ${WW}/WWSaveLoad ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/Wwutil)

add_lib_test(test_wwutil wwutil wwmath wwlib wwdebug)
target_include_directories(test_wwutil PRIVATE ${WW}/Wwutil ${WW}/WWLib ${WW}/WWMath)

# Lib/Clock.h is header-only and links nothing, so this runs on both platforms from day one -
# which matters, because the macOS half of that header is the half nobody has ever executed.
add_lib_test(test_clock)

add_lib_test(test_compression compression)

# .w3d on-disk layout pins.  Deliberately OUTSIDE the ZH_PLATFORM_WINDOWS guard
# below: it is header-only, asserts sizeof/offsetof and links nothing but the
# harness, so it builds on a platform where ww3d2 itself does not compile yet -
# which is the whole of M1 and the only time these asserts can still catch
# something.  See docs/mac-port/B7-w3d-layout.md.
add_lib_test(test_w3dlayout)
target_include_directories(test_w3dlayout PRIVATE
  # Libraries/Include is here because always.h reaches Platform/MSVCCompat.h, which B3 added after
  # B7 wrote this target.  Neither branch could see the other; it only fails once both are merged.
  ${CMAKE_CURRENT_SOURCE_DIR}/Libraries/Include
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil)

# Every test from here to test_pixelcentre links the renderer or a Direct3D backend, so they exist
# only where those targets do.  D2 through D5 bring the Metal side back with tests of its own.
if(ZH_PLATFORM_WINDOWS)
add_lib_test(test_ww3d2 ww3d2 wwsaveload wwlib wwmath wwdebug)
target_include_directories(test_ww3d2 PRIVATE
  ${WW}/WW3D2 ${WW}/WWLib ${WW}/WWMath ${WW}/WWDebug ${WW}/WWSaveLoad ${WW}/Wwutil)


add_lib_test(test_ffshader ffshader)
add_lib_test(test_dx11device dx11device)
add_lib_test(test_dx11state dx11state)
add_lib_test(test_dx11layout dx11layout)
add_lib_test(test_ffvertex ffvertex)
add_lib_test(test_ffvertexcompile ffvertex ffshader)
# The generated vertex program run against the fixed-function pipeline it replaces, on a real
# Direct3D 9 device.  It skips itself on a machine with no device rather than failing.
add_lib_test(test_ffvertexd3d9 ffvertex d3d9 user32)
add_lib_test(test_ffshadercompile ffshader)
add_lib_test(test_dx11pipeline dx11device dx11state dx11layout ffshader ffvertex)
add_lib_test(test_dx11resource dx11resource dx11device)
add_lib_test(test_engineshader engineshader)
add_lib_test(test_dx11backend dx11backend)
add_lib_test(test_dx11post dx11post dx11device user32)
add_lib_test(test_dx11twin dx11twin dx11device)
add_lib_test(test_dx11texture dx11texture dx11device user32)
# Both runtimes in one binary, so where a triangle edge lands is a measurement rather than a reading
# of two sets of documentation that describe the same thing in different words.
add_lib_test(test_pixelcentre dx11backend dx11resource dx11device d3d9 user32)
endif()  # ZH_PLATFORM_WINDOWS - renderer and Direct3D backend tests

# wwdownload, debug and eabrowserdispatch are Windows-only libraries above, so their tests are too.
if(ZH_PLATFORM_WINDOWS)
add_lib_test(test_wwdownload wwdownload)
target_include_directories(test_wwdownload PRIVATE ${WW}/WWDownload)

add_lib_test(test_debug debuglib)

add_lib_test(test_support benchmark eabrowserdispatch)
endif()  # ZH_PLATFORM_WINDOWS - Windows-only support library tests

# Phase 3 checkpoint: boots the memory manager, name keys and file system, then
# runs a real INI file through the parser.
add_lib_test(test_gameengine gameengine)
# T1's terrain golden against the moved code (GameLogic/WorldHeightMapData, TerrainHeightSampling):
# nine shipped maps out of the install's archives, which must sample exactly as Tests/terrain_golden.txt
# says the original W3D code did.  Needs ZH_GAME_DATA; Skipped (77) without it.
add_lib_test(test_terrain_golden gameengine)
target_compile_definitions(test_terrain_golden PRIVATE
  TERRAIN_GOLDEN_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Tests/terrain_golden.txt")
set_target_properties(test_terrain_golden PROPERTIES CXX_STANDARD 17)
set_tests_properties(test_terrain_golden PROPERTIES SKIP_RETURN_CODE 77)
# test_gameengine's stubs: the exe's names everywhere, and on Windows the device layer's stand-ins,
# which off Windows PosixDevice has for real.
target_sources(test_terrain_golden PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_terrain_golden PRIVATE posixdevice)
endif()
if(ZH_GAME_DATA)
  set_tests_properties(test_terrain_golden PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()
# T1c (defect 17): the water grid's step, moved to the top of each logic frame, against the original
# code's output under EA's one-pass-per-frame loop (Tests/water_grid_oracle.cpp), under catch-up too.
add_lib_test(test_water_grid gameengine)
target_sources(test_water_grid PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_water_grid PROPERTIES CXX_STANDARD 17)
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_water_grid PRIVATE posixdevice)
endif()
# Off Windows the device layer's hooks (render stand-ins, the CD manager) are PosixDevice's, where on
# Windows the test's own stubs stand in for gameenginedevice, which it does not link (B6).
if(ZH_PLATFORM_POSIX)
  target_link_libraries(test_gameengine PRIVATE posixdevice)
endif()
# the well-known Dict keys and a handful of device/exe entry points live in
# GameEngineDevice, which Phase 4 ports; stand-ins until then.
target_sources(test_gameengine PRIVATE ${TESTS}/test_gameengine_stubs.cpp)
set_target_properties(test_gameengine PROPERTIES CXX_STANDARD 17)
# simulation_uses_no_runtime_trig reads the engine's own sources back off disk to prove nothing in
# GameLogic or Common has gone back to calling sinf().
# fxlist_reforged_ini_parses_and_keeps_its_light runs the fork's own FXList file through the real
# INI parser, so it needs the tracked master rather than whatever sits in Run/.
# the_options_menu_buttons_all_wear_one_font reads the tracked layout master, not Run/'s copy.
# the_spectator_page_names_only_on_off_options reads the tracked page master the same way.
target_compile_definitions(test_gameengine PRIVATE
  GAMEENGINE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/GameEngine"
  FXLIST_REFORGED_INI="${CMAKE_CURRENT_SOURCE_DIR}/Data/INI/FXListReforged.ini"
  OPTIONS_MENU_WND="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Menus/OptionsMenu.wnd"
  SPECTATOR_HTML="${CMAKE_CURRENT_SOURCE_DIR}/Data/Window/Html/Spectator.html"
  PLATE_TEXTURE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/Data/Art/Textures")

# The three hand-written self-checks predate the harness; run them from ctest
# too so one command covers everything.
add_test(NAME detround_selfcheck COMMAND detround_selfcheck)
if(ZH_PLATFORM_POSIX)
  add_test(NAME fpucontrol_selfcheck COMMAND fpucontrol_selfcheck)
endif()
add_test(NAME d3dxportable_selfcheck COMMAND d3dxportable_selfcheck)
add_test(NAME wwmath_selfcheck COMMAND wwmath_selfcheck)
add_test(NAME compression_selfcheck COMMAND compression_selfcheck)
# The device and codec smoke tests below belong to targets only a Windows configuration defines.
if(ZH_PLATFORM_WINDOWS)
# dx9_smoke creates a device the way the renderer does, with nothing next to it to
# translate anything, and puts the D3D8 calls that have no D3D9 counterpart through their
# replacements.  "-msaa 4" adds the multisampled back buffer and the plain render target
# with a depth buffer of its own that DX8Wrapper::_Get_Non_MultiSampled_Depth_Buffer pairs
# for the screen filters.
add_test(NAME dx9_smoke COMMAND dx9_smoke)
add_test(NAME dx9_smoke_msaa COMMAND dx9_smoke -msaa 4)

# bink_smoke decodes a real .bik with the player this build links and compares what comes out
# against the values read straight from the file's own header, so a wrong field offset or a wrong
# surface constant shows up as a mismatch.  It runs from Run/, where the movies and the FFmpeg DLLs
# are; without game data it prints "skip".
add_executable(bink_smoke ${TESTS}/bink_smoke.c)
target_include_directories(bink_smoke PRIVATE ${WW}/Bink/include)
target_compile_definitions(bink_smoke PRIVATE BINK_LINKED_PLAYER)
target_link_libraries(bink_smoke PRIVATE binkvideo)
add_test(NAME bink_smoke COMMAND bink_smoke Data/Movies/GC_Background.bik
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Run)

# miles_smoke plays a tone through the audio backend and waits for the end-of-sample callback,
# which only arrives once a voice has played the buffer out.
add_executable(miles_smoke ${TESTS}/miles_smoke.cpp)
target_include_directories(miles_smoke PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_smoke PRIVATE milesaudio)
# From Run/, where the FFmpeg DLLs the backend decodes with are.
add_test(NAME miles_smoke COMMAND miles_smoke
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../Run)
endif()  # ZH_PLATFORM_WINDOWS - device and codec smoke tests

if(ZH_PLATFORM_POSIX)
# The audio backend's checks off Windows, on miniaudio's null backend so they run with no audio
# hardware: a device that still calls the mix in real time.  miles_smoke is the same file Windows
# runs.  test_miles_miniaudio listens to the mix through AIL_ex_start_capture and measures what
# miles_xaudio2 would have produced; the half of it that needs the game's .big files reads them from
# ZH_GAME_DATA (a folder holding generals/ and zerohour/, read only) and prints "skip" without it.
set(ZH_GAME_DATA "" CACHE PATH "Folder with a Zero Hour install's generals/ and zerohour/, for tests that read game data")
add_executable(miles_smoke ${TESTS}/miles_smoke.cpp)
target_include_directories(miles_smoke PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_smoke PRIVATE milesaudio)
add_test(NAME miles_smoke COMMAND miles_smoke)
set_tests_properties(miles_smoke PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")

add_executable(test_miles_miniaudio ${TESTS}/test_miles_miniaudio.cpp)
target_include_directories(test_miles_miniaudio PRIVATE ${WW}/Miles6/include ${LIBSRC}/miniaudio)
target_link_libraries(test_miles_miniaudio PRIVATE test_harness milesaudio miniaudio)
add_test(NAME test_miles_miniaudio COMMAND test_miles_miniaudio)
# ZH_DATA_DIR is set only when ZH_GAME_DATA was configured.  Setting it unconditionally overwrote a
# ZH_DATA_DIR from the caller's environment with an empty string, and the data half skipped silently.
if(ZH_GAME_DATA)
  set_tests_properties(test_miles_miniaudio PROPERTIES
    ENVIRONMENT "ZH_AUDIO_BACKEND=null;ZH_DATA_DIR=${ZH_GAME_DATA}")
else()
  set_tests_properties(test_miles_miniaudio PROPERTIES ENVIRONMENT "ZH_AUDIO_BACKEND=null")
endif()

# For human ears, not ctest: plays one sound from a .big archive (or a loose file) on the real
# device, through the same API, at a chosen volume and pan.
add_executable(miles_listen ${TESTS}/miles_listen.cpp)
target_include_directories(miles_listen PRIVATE ${WW}/Miles6/include)
target_link_libraries(miles_listen PRIVATE milesaudio)
endif()  # ZH_PLATFORM_POSIX - audio backend checks

if(ZH_PLATFORM_POSIX)
# The engine's paths against a POSIX file system (C1): the default volume, and on macOS a
# case-sensitive APFS image it creates, since neither macOS's default volume nor the Steam installs'
# exFAT tells case apart.  Linux's runs are case-sensitive already.
add_lib_test(test_posixpath wwlib wwdebug)
target_include_directories(test_posixpath PRIVATE ${WW}/WWLib ${WW}/WWDebug)

# PosixLocalFileSystem through the engine's own interface (C1): built from the real memory manager,
# LocalFile, File and PosixDevice sources with gameengine's include paths and definitions, because
# gameengine does not link on macOS yet.  The few names those sources reach and the test does not are
# in its _stubs.cpp, which says why each one is there.  Not on Linux yet, for fpucontrol_selfcheck's
# reason (BaseType.h's min/max against libstdc++); registered DISABLED there.
set(ZH_POSIXLFS_SOURCES_NO_MAIN
  ${GE}/Source/Common/System/GameMemory.cpp ${GE}/Source/Common/System/MemoryInit.cpp
  ${GE}/Source/Common/System/CriticalSection.cpp ${GE}/Source/Common/System/JobSystem.cpp
  ${GE}/Source/GameLogic/System/FPUControl.cpp ${GE}/Source/Common/System/ExecutableDirectory.cpp
  ${GE}/Source/Common/System/SubsystemInterface.cpp ${GE}/Source/Common/System/AsciiString.cpp
  ${GE}/Source/Common/System/WideCharFns.cpp ${GE}/Source/Common/System/File.cpp
  ${GE}/Source/Common/System/LocalFile.cpp ${GE}/Source/Common/System/LocalFileSystem.cpp
  ${GE}/Source/Common/System/RAMFile.cpp ${GE}/Source/Common/System/registry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Common/PosixLocalFileSystem.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/PosixDevice/Common/PosixLocalFile.cpp)
# The same list with the test's own two files, as test_registryfile and test_wwdownload_registry take it.
set(ZH_POSIXLFS_SOURCES
  ${TESTS}/test_posixlocalfilesystem.cpp ${TESTS}/test_posixlocalfilesystem_stubs.cpp ${ZH_POSIXLFS_SOURCES_NO_MAIN})
add_executable(test_posixlocalfilesystem ${ZH_POSIXLFS_SOURCES})
target_include_directories(test_posixlocalfilesystem PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_posixlocalfilesystem PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_posixlocalfilesystem PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_posixlocalfilesystem PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_posixlocalfilesystem COMMAND test_posixlocalfilesystem)
if(ZH_PLATFORM_LINUX)
  set_target_properties(test_posixlocalfilesystem PROPERTIES EXCLUDE_FROM_ALL TRUE)
  set_tests_properties(test_posixlocalfilesystem PROPERTIES DISABLED TRUE)
endif()

# Registry.ini's reader and writer (Common/RegistryFile.h, registry.cpp's POSIX half), from the same
# engine sources as test_posixlocalfilesystem, for the same reason, and not on Linux for the same one.
set(ZH_REGISTRYFILE_SOURCES ${ZH_POSIXLFS_SOURCES})
list(REMOVE_ITEM ZH_REGISTRYFILE_SOURCES ${TESTS}/test_posixlocalfilesystem.cpp)
list(APPEND ZH_REGISTRYFILE_SOURCES ${TESTS}/test_registryfile.cpp)
add_executable(test_registryfile ${ZH_REGISTRYFILE_SOURCES})
target_include_directories(test_registryfile PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_registryfile PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_registryfile PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_registryfile PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_registryfile COMMAND test_registryfile)
if(ZH_PLATFORM_LINUX)
  set_target_properties(test_registryfile PROPERTIES EXCLUDE_FROM_ALL TRUE)
  set_tests_properties(test_registryfile PROPERTIES DISABLED TRUE)
endif()

# WWDownload's registry functions and FormatURLFromRegistry off Windows (WWDownloadRegistryPosix.cpp)
# over this user's Registry.ini, read back through registry.cpp's own getters too (B6).  Writes only
# into a folder in the build tree.  POSIX only: on Windows they are wwdownload's, over the registry.
if(ZH_PLATFORM_POSIX)
  set(ZH_WWDOWNLOAD_REGISTRY_SOURCES ${ZH_REGISTRYFILE_SOURCES})
  list(REMOVE_ITEM ZH_WWDOWNLOAD_REGISTRY_SOURCES ${TESTS}/test_registryfile.cpp)
  list(APPEND ZH_WWDOWNLOAD_REGISTRY_SOURCES ${TESTS}/test_wwdownload_registry.cpp
    ${GE}/Source/Common/System/WWDownloadRegistryPosix.cpp)
  add_executable(test_wwdownload_registry ${ZH_WWDOWNLOAD_REGISTRY_SOURCES})
  target_include_directories(test_wwdownload_registry PRIVATE
    $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
  target_compile_definitions(test_wwdownload_registry PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
  set_target_properties(test_wwdownload_registry PROPERTIES CXX_STANDARD 17)
  target_link_libraries(test_wwdownload_registry PRIVATE test_harness wwlib wwdebug)
  add_test(NAME test_wwdownload_registry COMMAND test_wwdownload_registry)
  if(ZH_PLATFORM_LINUX)
    set_target_properties(test_wwdownload_registry PROPERTIES EXCLUDE_FROM_ALL TRUE)
    set_tests_properties(test_wwdownload_registry PROPERTIES DISABLED TRUE)
  endif()
endif()

# The user's folders off Windows (C1 (e)): the user data directory and the Desktop, EarlyOptions.h's
# POSIX half.  Writes only under $TMPDIR.
add_executable(test_userfolders ${TESTS}/test_userfolders.cpp)
target_include_directories(test_userfolders PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_userfolders PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_userfolders PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_userfolders PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_userfolders COMMAND test_userfolders)

# EarlyCommandLine.h off Windows: the options read before the parser exists, from the process's own
# argv, before main and after (C2).  The arguments are the test's input; see the test file.
add_executable(test_early_command_line ${TESTS}/test_early_command_line.cpp)
target_include_directories(test_early_command_line PRIVATE $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES>)
target_compile_definitions(test_early_command_line PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_early_command_line PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_early_command_line PRIVATE test_harness wwlib wwdebug)
# test_harness takes the first argument as its name filter; the options follow it.
add_test(NAME test_early_command_line COMMAND test_early_command_line early_command_line
  -logPrefix pre -headless -root "/tmp/zh_ä" -jobthreads 3 -last)

# The BIG archive reader on POSIX, against the real game data under rule 9 (C1 (f)): every path's
# owning archive against an independent reading of the mount order, and the bytes of a sample against
# an independent parser.  Built from the real sources, as test_posixlocalfilesystem is.  Needs
# ZH_GAME_DATA; without it the test is Skipped (77).
add_executable(test_bigfilesystem ${TESTS}/test_bigfilesystem.cpp ${TESTS}/test_bigfilesystem_stubs.cpp
  ${ZH_POSIXLFS_SOURCES_NO_MAIN}
  ${GE}/Source/Common/System/ArchiveFileSystem.cpp ${GE}/Source/Common/System/ArchiveFile.cpp
  ${GE}/Source/Common/System/StreamingArchiveFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFile.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Source/Win32Device/Common/Win32BIGFileSystem.cpp)
target_include_directories(test_bigfilesystem PRIVATE
  $<TARGET_PROPERTY:gameengine,INCLUDE_DIRECTORIES> ${CMAKE_CURRENT_SOURCE_DIR}/GameEngineDevice/Include)
target_compile_definitions(test_bigfilesystem PRIVATE $<TARGET_PROPERTY:gameengine,COMPILE_DEFINITIONS>)
set_target_properties(test_bigfilesystem PROPERTIES CXX_STANDARD 17)
target_link_libraries(test_bigfilesystem PRIVATE test_harness wwlib wwdebug)
add_test(NAME test_bigfilesystem COMMAND test_bigfilesystem)
set_tests_properties(test_bigfilesystem PROPERTIES SKIP_RETURN_CODE 77)
if(ZH_GAME_DATA)
  set_tests_properties(test_bigfilesystem PROPERTIES ENVIRONMENT "ZH_DATA_DIR=${ZH_GAME_DATA}")
endif()
if(ZH_PLATFORM_LINUX)
  set_target_properties(test_bigfilesystem PROPERTIES EXCLUDE_FROM_ALL TRUE)
  set_tests_properties(test_bigfilesystem PROPERTIES DISABLED TRUE)
endif()

# The POSIX half of the Wine listing oracle (Tests/fs_oracle.cpp; its Windows half is built with
# mingw-w64 and run under Wine, as C1's task file records).
add_executable(fs_oracle ${TESTS}/fs_oracle.cpp)
target_include_directories(fs_oracle PRIVATE ${WW}/WWLib)
target_link_libraries(fs_oracle PRIVATE wwlib wwdebug)

# The POSIX twin of test_ffshadercompile and test_ffvertexcompile: every generated program under the
# SDL3 GPU target through glslang, spirv-val, SPIRV-Cross and, on macOS, the Metal driver (D3).
# spirv-val is a tool on the machine, not something this build makes; without one the test prints
# SKIPPED for that step and says how many it did not check.
find_program(ZH_SPIRV_VAL spirv-val)
add_lib_test(test_shader_sdl ffvertex engineshader sdl3shadercompile)
if(ZH_SPIRV_VAL)
  set_tests_properties(test_shader_sdl PROPERTIES ENVIRONMENT "ZH_SPIRV_VAL=${ZH_SPIRV_VAL}")
endif()

# D6a: the glyph rasteriser against the font files' own hdmx, VDMX and gasp, read by the test's own
# parser, plus a regression golden keyed to the files' versions.  macOS's fonts; Linux later.
add_lib_test(test_glyph_rasteriser glyphrasteriser)

# The golden dump behind D3's byte-identical proof: every program in Tests/shader_cases.h, every
# target, one file each.  A tool, not a test: two runs are compared with diff -r.
add_executable(ffshader_dump ${TESTS}/ffshader_dump.cpp)
target_include_directories(ffshader_dump PRIVATE ${TESTS})
target_link_libraries(ffshader_dump PRIVATE ffvertex engineshader)
endif()  # ZH_PLATFORM_POSIX - shader generator checks

# Tools/bigfile.py reads and writes the game's BIG archives (that is how the shipped INIs
# come out for editing).  Its selfcheck round-trips synthetic files, so it needs no game
# data - but it does need an interpreter, so it is only registered when one is present.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  # Every #include in the POSIX build must name its file in the file's own case.  Windows and a
  # default macOS volume fold case and Linux does not, so a miscased include builds on the first two
  # and is a missing header on the third; the first Linux build found 298.  This catches them on a
  # Mac, from compile_commands.json, without Linux.  POSIX only: that is where the file is exported.
  # Nothing the POSIX build compiles may carry text as wchar_t - four bytes here, two on Windows -
  # and no literal anywhere in the engine may hold a raw non-ASCII byte, which MSVC and clang read as
  # different characters.  WideChar is char16_t (B1); this finds what the compiler cannot, such as an
  # L'x' that converts silently.  Exceptions, each with its reason, in Tools/widechar_check_allow.txt.
  if(ZH_PLATFORM_POSIX)
    add_test(NAME widechar_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/widechar_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  if(ZH_PLATFORM_POSIX)
    add_test(NAME include_case_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/include_case_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
    # Nothing may define _UNIX: not a vendored header (GameSpy's did, until vendor.sh renamed it),
    # not an engine header, not a compile flag.  Scans the vendored sources too, which git and this
    # machine's grep both skip, and plants a definition first to prove it can see one.
    add_test(NAME unix_define_check
             COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/unix_define_check.py
                     ${CMAKE_BINARY_DIR}/compile_commands.json ${CMAKE_CURRENT_SOURCE_DIR})
  endif()
  add_test(NAME bigfile_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/bigfile.py selfcheck)
  # Tools/vfx_tune.py rewrites those INIs in place; its selfcheck runs both rule sets over a
  # synthetic one and checks the block parser did not eat an End or duplicate a key.
  add_test(NAME vfx_tune_selfcheck
           COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/Tools/vfx_tune.py selfcheck)
  # The options screen is three tracked files that have to agree: TheOptionCatalog names a widget,
  # Data/Window/Menus/OptionsMenu.wnd has to carry it, and Data/Patch.str has to carry its label,
  # its tooltip and every combo box entry.  Nothing at build time notices a typo in any of them -
  # the control is just never filled in, or the screen draws the key name - so it is checked here.
  add_test(NAME optionsmenu_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/optionsmenu_layout.py selfcheck)
  # The peace time combo box is the same three-file agreement, in the two network lobby layouts:
  # the menu code names ComboBoxPeaceTime, the layouts have to carry it, and Patch.str has to carry
  # the entry captions, the tooltip, the corner countdown and the announcements.  A missing control
  # is silent - the menu null checks it and the option just never appears - so it is checked here.
  add_test(NAME peacetime_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/peacetime_layout.py selfcheck)

  # The lobby settings tab moves the host's settings onto a page of their own in all three lobbies,
  # and takes EA's superweapon checkbox out.  Same three-file agreement, plus one more failure the
  # null checks make silent: a control left behind on the old row would be drawn under the chat log.
  add_test(NAME lobbysettings_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/lobbysettings_layout.py selfcheck)

  # The skirmish and LAN rooms are laid out on one grid by arithmetic, and the mistake arithmetic
  # makes is silent: a control drawn over its neighbour, or a button a few pixels off the edge the
  # rest of its column stands on.  Both are checked here, with every window the menu code names.
  add_test(NAME lobbyroom_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/lobbyroom_layout.py selfcheck)

  # A translation is a .str of thousands of lines, and one printf directive lost or reordered in any
  # of them reads the wrong argument the moment that line is formatted.  The check parses the tracked
  # Turkish master the way GameText.cpp does, and against Run/EnglishZH.big when it is there.
  add_test(NAME gametext_selfcheck
           COMMAND ${Python3_EXECUTABLE}
                   ${CMAKE_CURRENT_SOURCE_DIR}/Tools/gametext.py selfcheck)
endif()

# The x86_64/arm64 differential harness.  Rosetta 2 runs an x86_64 binary on an Apple Silicon Mac
# and clang cross-compiles to it, so one machine can build the same source for both architectures
# and compare what they compute.  That recovers the architecture half of the determinism question
# without waiting for a Windows machine - and only that half.  It is clang on both sides, so it
# proves nothing about MSVC's codegen, its optimiser or its 32-bit long; Tests/arch_diff/
# run_arch_diff.sh says so at length and should be read before anyone calls a green run here
# "determinism verified".
#
# It skips rather than fails where the cross toolchain or Rosetta is absent, because it is a
# capability of a developer's machine and not a requirement of the build.  ctest reads 77 as a skip.
add_test(NAME arch_differential
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/arch_diff/run_arch_diff.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_BINARY_DIR}/arch_diff)
set_tests_properties(arch_differential PROPERTIES SKIP_RETURN_CODE 77)

# The D3DX oracle: runs Microsoft's own D3DXVec4Transform machine code under Rosetta and compares
# d3dxportable.h against it.  It needs d3dx9_43.dll, which this repository does not carry, so it
# reads ZH_D3DX9_X64 at run time (a Steam install's Jun2010_d3dx9_43_x64.cab, or the DLL) and skips
# without it.  A green run matches two of the DLL's three bodies, not "Windows";
# Tests/d3dx_oracle/run_d3dx_oracle.sh says why.
add_test(NAME d3dx_oracle
         COMMAND ${CMAKE_CURRENT_SOURCE_DIR}/Tests/d3dx_oracle/run_d3dx_oracle.sh
                 ${CMAKE_CURRENT_SOURCE_DIR}
                 ${CMAKE_CURRENT_BINARY_DIR}/d3dx_oracle)
set_tests_properties(d3dx_oracle PROPERTIES SKIP_RETURN_CODE 77)

if(RELEASE_DEBUG_LOGGING)
  target_compile_definitions(gameengine PRIVATE RELEASE_DEBUG_LOGGING)
  if(ZH_PLATFORM_WINDOWS)
    target_compile_definitions(gameenginedevice PRIVATE RELEASE_DEBUG_LOGGING)
    target_compile_definitions(generals PRIVATE RELEASE_DEBUG_LOGGING)
  else()
    # The same off Windows, for the same reason: the device layer and the exe log, and a class laid out
    # under DEBUG_LOGGING (ScriptEngine's timing fields) has to be laid out alike in every TU that uses it.
    target_compile_definitions(posixdevice PRIVATE RELEASE_DEBUG_LOGGING)
    target_compile_definitions(sdldevice PRIVATE RELEASE_DEBUG_LOGGING)
    target_compile_definitions(w3ddevice PRIVATE RELEASE_DEBUG_LOGGING)	# gameenginedevice's W3D half
    target_compile_definitions(generals PRIVATE RELEASE_DEBUG_LOGGING)
  endif()
endif()

# THREADING-ROADMAP.md section 0: the measurement build.  EA's PerfGather call sites are all
# over the frame already, but PerfTimer.h defines NO_PERF_TIMERS in *both* branches of its
# #if, so PERF_TIMERS is off in every configuration and every USE_PERF_TIMER compiles to
# nothing.  This switch turns them on without touching the shipping default: the timers read
# the TSC on every scope entry and exit, which is not free, so OFF is the right default and
# any perf claim has to say which build it came from.
#
# Only the three targets that include PerfTimer.h get the define.  It must not go on globally:
# WWVegas' headers reach GameEngine's Debug.h, and a mismatched define across libraries is how
# you get a link error at best and a silently different struct at worst.
option(PERF_TIMERS "Compile EA's PerfGather scopes in (measurement build, not for shipping)" OFF)
if(PERF_TIMERS)
  target_compile_definitions(gameengine PRIVATE PERF_TIMERS)
  # gameenginedevice and generals are only defined in a Windows configuration; see the platform
  # split at the top of this file.
  if(ZH_PLATFORM_WINDOWS)
    target_compile_definitions(gameenginedevice PRIVATE PERF_TIMERS)
    target_compile_definitions(generals PRIVATE PERF_TIMERS)
    # A measurement build is not the build anyone plays, and its post-build step copies into the
    # same Run/ directory the shipping one does.  Renaming the output keeps the two side by side -
    # which is also the only way to measure one against the other without rebuilding in between
    # (ai-batch.ps1 -Exe takes the name).
    set_target_properties(generals PROPERTIES OUTPUT_NAME generals_perf)
    message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time; exe is generals_perf.exe")
  else()
    message(STATUS "PERF_TIMERS ON - measurement build, timers cost real time")
  endif()
endif()

# Every test's own user data directory, in the build tree (the harness's guard, test_user_data_guard.cpp,
# stops any test binary started without one).  Here, after the last add_test, so no test is missed and
# no later set_tests_properties replaces it.  One folder per test so tests run in parallel do not share
# an Options.ini or a Registry.ini.  Off Windows only: there the engine does not read the variable.
if(ZH_PLATFORM_POSIX)
  get_property(_zh_tests DIRECTORY PROPERTY TESTS)
  foreach(_zh_test IN LISTS _zh_tests)
    set_property(TEST ${_zh_test} APPEND PROPERTY ENVIRONMENT
      "ZH_USER_DATA_DIR=${CMAKE_BINARY_DIR}/test-user-data/${_zh_test}")
  endforeach()
endif()

# Say out loud which tests this configuration does not run.  A test that is
# absent from `ctest` looks identical to one that passed, and on a port where
# half the tree does not compile yet that distinction is the whole point.
get_property(_zh_deferred GLOBAL PROPERTY ZH_DEFERRED_TESTS)
if(_zh_deferred)
  list(LENGTH _zh_deferred _zh_deferred_count)
  string(REPLACE ";" ", " _zh_deferred_text "${_zh_deferred}")
  message(STATUS "ctest: ${_zh_deferred_count} test(s) registered DISABLED on this platform "
                 "- ${_zh_deferred_text}")
  message(STATUS "ctest: they are built EXCLUDE_FROM_ALL and will not link until their task lands")
endif()
