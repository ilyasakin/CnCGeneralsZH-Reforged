# W1 (workers): the two-host LAN test, finer and thinkerer

Not to be confused with `W1-wwlib.md` (M1's wwlib task): this W1 is the PM's first worker assignment,
run by -47 on 2026-09-26/27 from this Mac over ssh, with no game run on this Mac.

| host | OS, toolchain | CPU | LAN address |
|:--|:--|:--|:--|
| finer | macOS 26.5.2, Apple clang (CommandLineTools), CMake 3.31.6 | arm64 (M3 Pro) | 192.168.1.106 |
| thinkerer | Arch Linux, gcc 16.2.1 with ld.lld | x86_64 (i5-8350U) | 192.168.1.21 (`wlan0`) |

Both hosts share one Wi-Fi LAN. The build on both is the run-only merge `w1-run` (f7106124):
feature/mac-port ec2898de plus feature/mac-port-nullthis 678887a1, feature/mac-port-lan-linux 869ffd1a
(merged as 33cafea0: the same tree, reworded) and feature/mac-port-w1-hosts 09fad008/aead57bf. Release
builds, and every game run is `-headless`.

## (a) ctest on thinkerer

First full run: 71 tests ran (75 listed, 4 disabled). 66 passed, 5 failed. The failures:

| test | cause | now |
|:--|:--|:--|
| test_lan_broadcast | Linux's SO_REUSEADDR lets a second socket bind the lobby's address and port | passes: feature/mac-port-lan-linux (merged) |
| packaging_resolution_check | the art archives were not cloned into thinkerer's worktree | passes once they are |
| widechar_check | no `unifdef` on the host | passes: unifdef installed (workers.md), 2152 files checked |
| test_gameengine | a stackdump | **open**: the Linux milestone |
| test_fontchars | fails | **open**: the Linux milestone |

The three fixed tests passed again on w1-run, and a `-V` run showed their checks ran: no skip hides
inside a pass. Skipped on thinkerer: the six SDL GPU selfchecks, ffprogram_values_check,
arch_differential, d3dx_oracle and d3dx_assemble_oracle.

**The Linux milestone's first list: six test binaries do not link on Linux** (ld.lld, "undefined
symbol", from thinkerer's build log; the Mac links them):

| binary | undefined on Linux |
|:--|:--|
| gametext_csf | AudioEventRTS::~AudioEventRTS() |
| test_premain_strings | AudioEventRTS::~AudioEventRTS() |
| test_premain_unicode | AudioEventRTS::~AudioEventRTS() |
| test_crash_reporting | AudioEventRTS::~AudioEventRTS(), AsciiString::freeBytes(), UnicodeString::releaseBuffer(), MemoryPool::freeBlock(void*), MemoryPoolFactory::createMemoryPool(...), TheMemoryPoolFactory |
| test_early_command_line | the same six as test_crash_reporting |
| test_sdl_platform | ApplicationHWnd, ApplicationIsBorderless, gAppPrefix, g_strFile, g_csfFile |

Add test_gameengine's stackdump and test_fontchars from the table above.

finer, for comparison, earlier in W1: 83 tests, 0 failed, 11 skips. After 681d6f9a the six GPU selfchecks
run on finer instead of skipping (-a9 measured 6/6); this document did not rerun them.

## The CRCs, side by side

`replay-check.sh`: two players, each seed recorded, played back and played again, on each host alone.

| run | finer (macOS, clang, arm64) | thinkerer (Linux, gcc, x86_64) |
|:--|:--|:--|
| seed 0, frame 1200 | 0x0177BEF6 | 0x0177BEF6 |
| seed 1, frame 1200 | 0x74C29560 | 0x74C29560 |
| seed 1, frame 12000 | 0x7C7DBA69 | 0x7C7DBA69 |
| two-host match, seed 3, frame 3000 (b) | 0x341D0C61 | 0x341D0C61 |
| the other host's replay, played here (b) | 0x341D0C61 | 0x341D0C61 |

The shipped 13.0 app bundle, launched on finer by replay-check `--app`, also gives 0x0177BEF6 at seed 0,
frame 1200, and its signature still verifies afterwards.

## (b) One match between the two hosts

`net-check.sh --peer <slot> --hosts 192.168.1.106,192.168.1.21` on each host: finer is slot 0 and
thinkerer slot 1. The match is seed 3, 2 AI, 3000 frames on Golden Oasis. Each side runs behind its own
guards: firewall (macOS socketfilterfw off; thinkerer's iptables INPUT policy ACCEPT, nft policy
accept), the machine lock, the ports, the farm and the install hash. The driver on this Mac starts
peer 1 only after peer 0 has printed that its game started.

```
PEER-RESULT slot=0 crc=0x341D0C61 frame=3000 mismatches=0 dropped=0 built=12 ended=0 back_crc=0x341D0C61 back_frame=3000 aligned="recorded from frame 0" oos=""
PEER-RESULT slot=1 crc=0x341D0C61 frame=3000 mismatches=0 dropped=0 built=12 ended=0 back_crc=0x341D0C61 back_frame=3000 aligned="recorded from frame 0" oos=""
```

The match was played twice with the same result: 146 s and 136 s the first time, before the dropped=
field existed, and 113 s and 107 s the second time, when it printed the lines above. Each host's replay was copied through this Mac (sha256 unchanged
across both hops) and played on the other host:

```
finer plays thinkerer's replay:  PLAY-RESULT crc=0x341D0C61 frame=3000 ended=0 aligned="recorded from frame 0" oos=""
thinkerer plays finer's replay:  PLAY-RESULT crc=0x341D0C61 frame=3000 ended=0 aligned="recorded from frame 0" oos=""
```

A replay carries the CRCs recorded every interval, and playback compares each one (oos is empty), so
each host reproduced the other's world at every recorded interval, not only at frame 3000.

**A lone peer passed, until dropped= was added.** A mistimed control left finer's peer 0 with no
partner. The game did not stop: it put up the disconnect screen after 20 s, dropped slot 1 on frame 30,
played on alone to frame 3000 (0x8B1CEC4B, 270 s), and reported `mismatches=0 ended=0`: a clean
PEER-RESULT. A deliberate lone peer 1 on thinkerer did the same (`disconnecting slot 0 on frame 30`,
0x7D0157A7 at 600). So the harness now counts the game's `ConnectionManager::disconnectPlayer -
disconnecting slot` lines as `dropped=`, and a peer passes only with 0 (b275425c). The same lone run
now prints `dropped=1` and exits 1; the real match prints `dropped=0` on both hosts. The results above
still stand without it: a lone run's CRC differs from the match's.

**The seed control:** peer 0 on seed 3 and peer 1 on seed 4, so one command stream starts two
different worlds (NET_CHECK_LIVE_TIMEOUT=300). The match must fail, and it failed on both sides:

```
PEER-RESULT slot=0 crc=0x8B1CEC4B frame=3000 mismatches=0 dropped=1 built=13 ended=0 ...   (finer, exit 1)
PEER-RESULT slot=1 crc=none frame=none mismatches=1 dropped=0 built=0 ended=2 ...           (thinkerer, exit 1)
```

thinkerer logged the CRC mismatch within 7 s, and the harness stopped it. finer never logged a mismatch
itself: it saw its partner go, dropped slot 1, and played on alone to 0x8B1CEC4B (the lone seed-3 CRC
again). Before dropped= existed, finer's half of this control would have passed. The field is what
makes both halves of a failed match fail.

## (c) The lobby's sockets across the two hosts (#29)

`lan_broadcast_probe` holds the lobby's two sockets as LANAPI builds them, through the real Transport
and UDP classes: the lobby socket on the host's address, and the broadcast listener
(UDP::BindForBroadcasts, which is the wildcard address on macOS and 255.255.255.255 on Linux). The
other host sends from its own lobby socket. Port 28086, so a running game's 8086 is untouched.

| direction | broadcasts (to 255.255.255.255) | directed (to the listener's address) |
|:--|:--|:--|
| finer to thinkerer | 20/20 plus 2/2, all on the listener, none on the unicast socket | 5/5 plus 1/1, all on the unicast socket |
| thinkerer to finer | 20/20 plus 1/2, all on the listener | 5/5 plus 1/1, all on the unicast socket |

The one lost broadcast was in a single three-message run. Wi-Fi sends broadcast frames with no
acknowledgement and no retry. The lobby re-announces every 10 s (LANAPI::s_resendDelta) and drops a game
it has not heard for twice that, so one lost announcement costs nothing and a game drops off the list
only after two in a row are lost. An earlier attempt in which finer heard nothing is not
counted: its 40 s window may have closed before thinkerer sent.

On Linux, a second lobby socket on an address and port that a running lobby holds now fails to bind,
where macOS lets it share. This is the fix itself (lan-linux): two lobbies on one Linux host need two
addresses, as on Windows. A Linux listener bound to 255.255.255.255 does not hear a subnet-directed
broadcast (192.168.1.255). That was measured, and it is recorded in the lan-linux commit. The game
never sends one (LANAPI.cpp:93).

## The null-check flag (feature/mac-port-nullthis, merged 70760dc4)

On finer, `-fno-delete-null-pointer-checks` grows `generals`' __text by 52,044 bytes (16,204,300 to
16,256,344; the file by 59,552). The three `if (this)` sites named in the commit were already kept by
clang, so clang was deleting other null checks: the flag also keeps a check that follows a dereference
of the same pointer. With the flag, the POSIX code is closer to MSVC's, which never deletes them. The
CRCs did not move at the frames tested (seed 0 at 1200, seed 1 at 12000). All 1,729 compile commands
carry the flag.

## What this cannot see

- Windows. No MSVC build or Windows peer took part; that is W2.
- More than two players, and a network other than this one Wi-Fi LAN. There was no injected latency or
  loss, and no MTU below Ethernet's.
- The LAN lobby's GUI, its join and its map transfer. The match used `-netgame`, and (c) tests the
  sockets under the lobby, not LANAPI's message loop above them.
- Frames and code paths that seeds 0, 1 and 3 never reach.
- The firewall gates read iptables' INPUT chain and nft's input hooks. They do not follow a jump into a
  sub-chain: thinkerer's `ts-input` (Tailscale) drops only 100.64.0.0/10 arriving off tailscale0, which
  was read by hand. macOS pf rules and third-party filters are not read.
- A single-host net-check at seed 3, frame 3000 was not run, so there is no same-machine CRC to set
  beside 0x341D0C61. The 0x3453DF90 of an earlier run on this Mac is at frame 1800, so it does not
  compare.
