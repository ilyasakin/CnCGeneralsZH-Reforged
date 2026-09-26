# L1 — LAN play on POSIX

- **Milestone:** M5 ("LAN over UDP is in scope")
- **Depends on:** B1 B4 B5 (the wire formats, done), N1 (the compatibility CRC, done), E1 E3 (the
  determinism gate and the architecture axis, done)
- **Blocks:** nothing in M5 but its own "it is a game"
- **Status:** done (-47, 2026-09-26): recon, then steps 1, 3 (defect #29), 2 (F2), 4 and 5 in the
  PM's order; step 6 deferred until E2 needs it. See "Close-out" at the end.

## Why

M5 lists LAN. Nobody had asked whether the network layer runs off Windows, whether two peers can
meet on one Mac without a person, or what a pass would prove. This file answers those three
questions by reading the code and measuring, before anything is built.

## 1. The network layer on POSIX today

**It compiles, links and plays.** `generals` on macOS links all of `GameNetwork/`. Three commits on
2026-09-26 (032b1b82, 8a8bd52a, 57988d42) finished the UNIX half that Westwood's `udp.cpp` already
had. Each was measured with the real objects and has an armed control.

| Windows call or assumption | What it does | On the Mac today |
|:--|:--|:--|
| `WSAStartup` / `WSACleanup` | winsock start-up (Transport, IPEnumeration) | under `_WIN32`; POSIX needs none |
| `socket`, `bind`, `sendto`, `recvfrom`, `select`, `setsockopt(SO_RCVBUF/SO_SNDBUF)` | the UDP socket (`udp.cpp`) | the same BSD calls; lengths are `socklen_t` |
| `ioctlsocket(FIONBIO)` | non-blocking socket | `fcntl(O_NONBLOCK)`, Westwood's own UNIX branch |
| `WSAGetLastError`, `WSAEWOULDBLOCK` | error codes | `errno` through `lastSocketError()`. An empty read is "nothing yet" (EWOULDBLOCK/EAGAIN), not an error, and a failed bind now reads as failed |
| `closesocket` | close | `close` |
| `setsockopt(SO_BROADCAST, BOOL)` | allow broadcasts | the same option with an `int` |
| `gethostname` + `gethostbyname` | the machine's LAN addresses (IPEnumeration) | `getifaddrs`: every IPv4 address on an interface that is up, ascending, loopback only when there is nothing else. `GetAdaptersInfo` is not used anywhere |
| `S_un.S_addr` | the address union | `s_addr`, winsock's own macro for the same member |
| broadcast reception on a socket bound to one address | the lobby's game announcements | **does not happen on macOS: finding F1 below** |
| winsock in `FirewallHelper.cpp` | Internet NAT probing (GameSpy mangler servers) | out of scope: GameSpy is dead, and LAN never calls it |

**Ports.** The lobby uses UDP 8086 (`LANAPI.cpp:51`) and the game uses 8088
(`NETWORK_BASE_PORT_NUMBER`). Every address uses one port; neither sets SO_REUSEADDR.

**Wire format.** This was settled before L1:
- B4 and B5 put `sizeof` and `offsetof` asserts on every packed wire struct: `TransportMessageHeader`
  6, `TransportMessage`, `LANMessage` 471, and the mangler structs.
- `NetworkDefs.h` asserts little-endian byte order off MSVC. arm64, x86_64 and Windows x64 are all
  little-endian.
- `GameMessage::Type` is pinned to `Int`, so clang's `unsigned` enum became MSVC's type.
- `WideChar` is `char16_t` and asserted to be 2 bytes.
- `NetPacket` sends message arguments as `sizeof(Bool)`, `sizeof(Real)`, `sizeof(Coord3D)`,
  `sizeof(ICoord2D)` and `sizeof(IRegion2D)` bytes. These match by ABI (`bool` is 1 byte and `float`
  4 on all three), but they are not asserted. That is build step 5.

**The compatibility CRC.** N1 took the executable's bytes out of `m_exeCRC`. A Mac and a Windows
build of the same source now agree at the lobby's version check.

### F1: a POSIX lobby never hears another machine's broadcasts

- `LanLobbyMenu` binds the lobby socket to one address through `SetLocalIP`: the Options choice, or
  the first address IPEnumeration finds.
- Game announcements go to 255.255.255.255:8086.
- Windows delivers a broadcast to a socket bound to a unicast address; `lan-play.ps1` depends on it.
  BSD sockets do not.

Measured on this Mac, sending from 192.168.1.103 to port 18086:

| listener bound to | to 255.255.255.255 | to 192.168.1.255 |
|:--|:--|:--|
| 192.168.1.103 (what the lobby does) | nothing | nothing |
| INADDR_ANY | received | received |
| 192.168.1.255 | — | received |

So a Mac in a LAN lobby would never list a Windows or Mac host's game. A game reached some other
way, such as direct connect or a directed reply, would still work.

Linux has the same socket semantics. That is by its documentation; Docker is held, so it isn't
measured here.

The fix is POSIX-only and small: a second lobby socket on INADDR_ANY:8086 for broadcasts
(SO_REUSEADDR plus SO_REUSEPORT, so that two copies on one host both hear them), beside the
unicast one. It is build step 3. Windows is unchanged.

## 2. Two peers on one Mac, with no window and no person

### Addresses: measured

| Idea | Result on macOS |
|:--|:--|
| Both on 127.0.0.1:8088 | the second bind fails, EADDRINUSE. And `GameInfo::getLocalSlotNum` picks the local slot by IP alone, so two copies on one address would both claim slot 0 |
| 127.0.0.1 and 127.0.0.2, as `lan-play.ps1` does | EADDRNOTAVAIL: macOS configures only 127.0.0.1 on lo0. An alias needs `sudo ifconfig lo0 alias 127.0.0.2`, and a person. Linux routes all of 127/8, which is untested here |
| SO_REUSEPORT on one address:port | BSD delivers each unicast datagram to one of the sockets, not to a chosen peer. That's no use for two players |
| **127.0.0.1 and one of the machine's own non-loopback addresses** | **works, with no root and no code change.** 127.0.0.1 ↔ 192.168.1.103 (en0) and 127.0.0.1 ↔ 10.211.55.2 (bridge104) delivered both ways on one port. en0 ↔ bridge104 did not. So which pairs work depends on the machine, and the pair should include 127.0.0.1 |

Two costs of the working route:
- It needs the machine to have an address that is up. A harness should skip with 77 when it
  doesn't.
- With the macOS application firewall on, an unsigned binary listening on a non-loopback address
  gets an "accept incoming connections" dialog. The firewall is off on this machine, so nothing
  appeared. A harness should check `socketfilterfw --getglobalstate` and skip rather than put up a
  dialog.

A code route that needs no second address is `-netgame ip:port` per slot, with the local slot taken
from `-netslot` instead of by IP. It is a dev-path change only, and it's the robust one for CI (E2).
It is build step 6, if E2 needs it.

### Driving it: nothing new is needed

- `-netgame <ip,ip…>`, `-netslot <n>`, `-netai <n>`, `-seed`, `-map`, `-teams` start a LAN match with
  no lobby: `LANAPI::StartAutomatedGame`, 458e7fc5.
- `-headless`, `-maxframes`, `-multiInstance`, `-logPrefix`, `ZH_USER_DATA_DIR`, `-root`/`-overlay`
  isolate each copy, as `replay-check.sh` does.
- `net_check.py` is its Windows ancestor: 127.0.0.x addresses, `-control` key presses, and `--prove`.
- `-lanlobby` with `-lanip`/`-lanname` opens the lobby, but it still needs clicks, or `-control`, to
  host and join. The lobby is not needed for the acceptance, and F1 is tested at the socket level.

### The measurement, 2026-09-26

Setup:
- The arm64 `build-mac/generals` of feature/mac-port 3db1807b, a Rule-9 farm and a staged overlay.
- Install guard: "ok: the install is as it was (1070 entries)".
- `ZH_HIDDEN_WINDOW=1 … -headless`: no window.
- Two copies at once, each with its own `ZH_USER_DATA_DIR`:
  `-netgame 127.0.0.1,192.168.1.103 -netslot 0|1 -netai 2 -map "Maps\Golden Oasis\Golden Oasis.map" -seed 3 -maxframes 3000`.

| | peer 0 (127.0.0.1) | peer 1 (192.168.1.103) |
|:--|:--|:--|
| slots | local 0, remote at C0A80167:8088 | local 1, remote at 7F000001:8088 |
| "CRC Mismatch" lines | 0 | 0 |
| HEADLESS CRC at frame 3000 | **0x341D0C61** | **0x341D0C61** |
| AI structures after frame 0 | 12 | 12 |
| replay | 189,127 bytes | 189,127 bytes |
| its replay played back alone | 0x341D0C61 at frame 3000 | 0x341D0C61 at frame 3000 |

The live match took 105 s, and both copies exited with 0.

### F2: a -netgame replay reports an out-of-sync it does not have

- Both playbacks log "Replay has gone out of sync on frame 31: recorded EA9BF312, played back
  AE684603".
- Yet each reaches the live world's CRC 2,969 frames later.
- The two peers recorded the same CRC, so they agree with each other. It is the playback check that
  disagrees at its first comparison.
- It is the shape 9126172a fixed for lobby-started network games: the frame-0 CRC is never
  recorded, and playback skips one entry for GAME_LAN.

Not yet known:
- whether the `-netgame` path records one entry more or fewer than the lobby path does;
- whether a lobby-started game shows it too;
- whether Windows does.

Build step 2 finds out. Until then, the check reads "no desync" from the live peers' CRC exchange
and the final world CRCs, not from the playback's in-sync line.

## 3. Acceptance, and whether it is feasible

**Feasible, and half of it is already measured:**
- Two headless Mac peers, one seeded LAN match to N frames, no "CRC Mismatch", the same final CRC:
  done above at N = 3000, arm64 against arm64.
- Each peer's replay played back to the same CRC as the other's: done above, apart from F2's
  message.
- **One peer x86_64 under Rosetta.** E1's architecture axis has already built the whole game for
  x86_64 (`CMAKE_OSX_ARCHITECTURES=x86_64`, FFmpeg cross-configured). Its replays cross-played with
  arm64 at 12,000 frames. For LAN, that build and the arm64 one play live against each other. It
  needs one extra build directory, about 2.1 GB (build-mac's size), with 8.7 GB free. It is to be
  deleted after the run.

**What the acceptance cannot see:**
- **A Windows peer.** MSVC's code generation, LLP64's 32-bit `long` and the MSVC CRT are all
  outside this Mac. Rosetta gives x86-64 semantics under clang, not Windows. The defect class E1
  found in `1UL << (dt - 1)` agreed across arm64 and x86_64 and differed only on Windows. Only a
  Windows peer, or a Windows-recorded replay, closes it.
- **Real multi-host LAN.** On one host, traffic between two local addresses never leaves the kernel,
  so there is no real latency, loss, reordering, MTU or switch. `-latAvg`, `-latNoise` and
  `-packetloss` simulate the first three. Lobby discovery between machines is F1, and a one-host run
  can't show it: the -netgame path skips the lobby.
- **NAT.** LAN has none, and Internet play (GameSpy) is out of scope.
- **The application firewall's dialog**, on a Mac where the firewall is on.
- **More than two humans.** `net_check.py` notes that four copies stall at frame 61 in the
  disconnect keepalive, on Windows; that isn't chased here.
- **The lobby's GUI flow, map transfer and chat.** None is exercised by `-netgame`.

## Build steps

1. **`Tools/net-check.sh`, a POSIX twin of `net_check.py` and a ctest.**
   - It uses replay-check's farm, overlay, install guard and user folders.
   - The peers are 127.0.0.1 plus the first non-loopback address that is up. It skips with 77
     without data, without such an address, or with the application firewall on.
   - It runs a seeded `-netgame` match with `-netai` AIs to N frames and checks that neither peer has
     "CRC Mismatch", that the final CRCs are equal, and that the AI built something (an idle match
     proves nothing).
   - Each peer's replay is played back to the same CRC.
   - Armed control: peer 1 gets a different `-seed`, so the start positions differ, and the check
     must report a mismatch.
   - `--arch x86_64 <generals>` runs one peer under Rosetta.
2. **F2:** find why a `-netgame` replay's first CRC comparison is off. Then fix it where it's
   wrong, or record why it's right.
3. **F1:** hear broadcasts on POSIX. Add a second lobby socket on INADDR_ANY:8086 with
   SO_REUSEADDR and SO_REUSEPORT, POSIX only. Test it at the socket level: a broadcast reaches both
   of two copies' lobby sockets, and a directed message reaches only its own. Include an armed
   control, and a `windows_view_diff` showing Windows unchanged.
4. **The cross-architecture proof:** step 1 with one peer x86_64, in one temporary build directory,
   deleted afterwards.
5. Assert the argument widths `NetPacket` sends (`Bool` 1, `Real` 4, `Coord3D` 12, `ICoord2D` 8,
   `IRegion2D` 16, `ObjectID` and `DrawableID` 4) beside the B4 asserts, so an MSVC difference fails
   the build.
6. Only if E2's runners have no second address: `-netgame ip:port` and the local slot from
   `-netslot`.

## Step 1 result: `Tools/net-check.sh` and ctest `net_check` (-47, 2026-09-26)

The harness is as step 1 describes, with the PM's constraints.
- **The second address** is found by a probe, never written in: the first up, non-loopback IPv4
  address (`ifconfig`, else `ip`) that delivers a datagram to and from 127.0.0.1. With none, it
  skips (77). Both game ports must be free on both addresses, or it skips and names them.
- **The firewall gate** comes first, before the probe listens. On macOS it reads socketfilterfw's
  global state, stealth mode and block-all, and skips (77) unless all three are off. Its own control
  uses fakes reporting each of the three on; each must skip before any copy starts.
  - It cannot see third-party filters (Little Snitch, LuLu), pf rules, or a firewall switched on
    after the check. The script's header says so.
- **A mismatch ends the match at once.** After "CRC Mismatch" a copy waits on its disconnect screen
  for good, so the harness stops both copies when either log shows one. Without that, the control
  ran into the time limit in every phase, about 15 minutes; with it, the control takes 30 s.
- **The replays** are played back only after a match that kept one world. F2's message is printed
  but does not fail the run until step 2 settles it.

- **Two later hardenings,** found by running it:
  - In a `ctest -j4` it came up Skipped in 0.6 s: the game's ports were taken, most likely by the same
    test in another checkout at the same moment. It now waits for busy ports (`NET_CHECK_PORT_WAIT`,
    600 s) and skips only if they stay taken. Checked both ways with a socket holding 8088: it skipped
    after a 4 s wait, and it went ahead when the port was freed during a 60 s wait.
  - A signal (a ctest timeout, ^C, a closed pipe) now exits through the cleanup trap. Checked with
    TERM mid-match: exit 130, both copies stopped, the work folder and logs gone, the install verified.

ctest `net_check` (TIMEOUT 900, in the `zh_install` lock with the other farm tests), `-V`, 138 s:

| check | result |
|:--|:--|
| 0. the firewall gate's control: fakes reporting on, stealth and block-all | each skips (77) before any copy starts |
| 1. seed 3, two AIs, Golden Oasis, 1800 frames, 127.0.0.1 and 192.168.1.103 | both 0x3453DF90 at frame 1800, 0 mismatches, 5 AI structures; both replays play back to 0x3453DF90 |
| 2. the control: the second copy on seed 4 | FAILED through the game's own "CRC Mismatch", stopped after 6 s of match; exit 1 |

### One net-check on the whole machine, and a descriptor guard (-47, 2026-09-26)

**The incident.** Two `net_check`s from two worktrees ran at once; ctest's `RESOURCE_LOCK` holds only
within one ctest. The copy that could not bind 8088 spun in `Transport::init`'s one-second bind retry,
and `UDP::Bind` leaked a socket per try. Two peers held 188k sockets, the machine's file table filled,
and every other process failed with "too many open files in system". -18 is fixing the leak in shared
code.

The harness now guards against it in three ways:
- **A machine-wide lock.** Before its probe it takes `flock(2)` on `/tmp/zhr-net-check.lock`
  (`NET_CHECK_LOCK_FILE`), waiting up to `NET_CHECK_LOCK_WAIT` (900 s) and then skipping with the holder
  named. The lock is taken through python3's `fcntl`, since macOS has no `flock(1)`, on descriptor 9,
  which the harness and both copies inherit. The kernel therefore holds the lock until every one of
  them has exited, a copy outliving a killed harness included, and releases it however they end. No
  PID file, nothing to go stale.
  - It protects only against harnesses that take it: an older checkout's `net-check.sh` does not.
- **A cap per copy.** Each copy runs under `ulimit -n` of `NET_CHECK_FD_LIMIT` (4096), so a leak ends
  in EMFILE for that copy, not in ENFILE for the machine. A polling watchdog alone could not stop it:
  the leak ran at about 94,000 sockets a second.
- **A watchdog.** Once either copy holds three quarters of the limit, both copies are stopped and the
  run fails with "DESCRIPTOR LEAK: process … held N descriptors".

**Checked** (`net_check` gains two checks):
- The lock:
  - held elsewhere past a 3 s wait, the harness skips, naming the holder, and starts no copy;
  - held for 6 s, it waits and then goes ahead.
- The guard: a stand-in second copy that opens sockets until refused.
  - It stopped at 4,092 ("Too many open files").
  - The machine's peak was 14,535 of 276,480.
  - The run stopped within 2 s with "DESCRIPTOR LEAK: process … held 4100 descriptors", and the
    stand-in was gone.
  - The stand-in's first version died on EMFILE opening its own report and looked inert. It now gives
    one descriptor back first, and holds the rest as a real leak would.

## Step 3 result: F1 fixed, defect #29 (-47, 2026-09-26)

README defect #29 has the full entry. In short:
- Each POSIX lobby keeps a wildcard listener on the lobby port (SO_REUSEADDR + SO_REUSEPORT) that
  passes only datagrams sent to 255.255.255.255.
- The lobby socket sets SO_REUSEADDR so that it can share the port with another copy's listener.
- `LANAPI::update` moves what the listener heard into the lobby's inbox. Each message then meets the
  own-address filter once.

Measured before the code, on macOS:
- With both copies' sockets laid out that way, a broadcast reached both listeners once each and neither
  unicast socket.
- Directed messages reached only their own socket.
- `IP_RECVDSTADDR` reported 255.255.255.255 for the broadcasts.
- A copy on 127.0.0.1 cannot send a broadcast at all (EADDRNOTAVAIL). A real lobby binds a real
  address, so play is not affected, but a one-host test has to broadcast from the non-loopback copy.

`test_lan_broadcast` (3 tests, 31 checks, 4 s) runs that layout through the real classes. It has a
control and three armed mutations (1, 7 and 2 checks fail). `windows_view_diff`: the six changed files
are identical as MSVC sees them; the new test is POSIX-only.

## Step 2 result: F2, the replay CRC check aligned to both kinds of recording (-47, 2026-09-26)

**Cause.** The recording is right; the check was stale.
- A transfer command such as MSG_LOGIC_CRC is sent only while the network is in-game
  (`Network.cpp:482`). The network leaves pregame only in `noteLogicFrameAdvance`, when the logic
  reaches frame 1.
- EA called that from `processCommand`, after frame 0's CRC had been met and deleted. So no network
  replay had frame 0's CRC, and 9126172a (Aug 28) made playback skip one.
- d9eccdda (Sep 22) calls it first in `Network::update`. Since then frame 0's CRC is sent and recorded
  in every network game, lobby or `-netgame`, on every platform. The blind skip then put every
  comparison one frame out.
- The first mismatch silences the check (`sawCRCMismatch`), so every LAN replay played back since then
  had no CRC check from frame 31 on.
- Measured with a trace build: over 294 comparisons, recorded equals computed with nothing skipped,
  and equals what the live copy generated from frame 0.

**Fix** (playback only; nothing in the simulation, on the wire or in the file format changes):
- `CRCInfo::readCRCFor` decides at a network replay's first comparison. If the head of the computed
  queue is the recorded CRC, it keeps it: "Replay CRCs: recorded from frame 0". If the next one is, it
  drops the head: "legacy: frame 0 missing", which fits retail replays and those from before
  d9eccdda. If neither is, it compares as it stands, so a desync on the very first CRC is still
  reported.
- The log names the alignment.
- The out-of-sync line's swapped labels are fixed.
- `WINDOWS-DEBT.md` has the row.
- EA's order was not restored (the PM rejected it): that would change the network and lose a CRC from
  new recordings.

**Checks**
- `test_gameengine`'s replay test covers a solo replay, a network one recorded from frame 0, a legacy
  one, and a desync on the first CRC. Armed: never dropping fails 5 checks; always dropping
  (9126172a's rule) fails 4.
- `net_check` now requires every playback to align "recorded from frame 0" and never report out of
  sync. Always dropping fails it at frame 31 on both replays, the original symptom.
- New third check, the check live to the end and both alignments in the real game:
  - copy 0's replay has its CRCs at frame 1500 changed by one bit, and is reported "out of sync on
    frame 1500" and nowhere earlier;
  - copy 1's replay has frame 0's CRC records removed, which makes it the shape of a legacy recording.
    It aligns "legacy: frame 0 missing" and plays back clean.

## Step 4 result: the cross-architecture LAN proof (-47, 2026-09-26)

- **The build.** One x86_64 build directory (`CMAKE_OSX_ARCHITECTURES=x86_64`, E1's recipe), with only
  `generals` built: 1.3 GB, under the 2.1 GB estimated. It had the same build fingerprint as the arm64
  build (0xA39CAFCC), so it was the same source.
- **Disk.** 8,165,068 KB free before the build and at least 6.4 GB free throughout, above the PM's
  4 GB floor. A watcher would have killed the build and deleted the folder below it. The folder was
  deleted right after the runs: 7,978,256 KB free after, other sessions' use included.
- **Both slot orders**, seed 3, two AIs, Golden Oasis, 3000 frames, `net-check.sh --peer1`:

| | copy 0 (127.0.0.1) | copy 1 (192.168.1.103) | result |
|:--|:--|:--|:--|
| A | arm64 | x86_64 under Rosetta | both 0x341D0C61 at frame 3000, 0 mismatches, 12 AI structures; both replays align "recorded from frame 0" and play back to it |
| B | x86_64 under Rosetta | arm64 | the same |
| control | arm64 | x86_64, on seed 4 | FAILED through the game's own "CRC Mismatch" after 8 s, exit 1 |

  0x341D0C61 is also what the arm64-only recon match ended on. Run A waited for the game's ports,
  which another session's run held, and then went ahead.
- **What this adds to E1's architecture axis:** the live lockstep between the two instruction sets
  over the real network code, not only a replay played on the other one.

## Step 5 result: the argument widths pinned (-47, 2026-09-26)

- `NetPacket.cpp` static_asserts the widths a game message's arguments travel at, on the wire and in
  the replay file: `Int` 4, `Real` 4, `Bool` 1, `ObjectID` 4, `DrawableID` 4, `UnsignedInt` 4,
  `Coord3D` 12, `ICoord2D` 8, `IRegion2D` 16, plus the 1- and 2-byte header types. That is beside what
  B4/B5 pinned (the packed structs, `GameMessage::Type`, `WideChar`).
- It compiles with clang on arm64 and x86_64. Armed: `Coord3D == 16` fails the compile, naming the
  argument type.
- MSVC compiles these for the first time; `WINDOWS-DEBT.md` has the row.

## Close-out

**What L1 proves, measured on one Mac:**
- The network layer runs off Windows: sockets, the address list, broadcast, the lockstep and the CRC
  exchange.
- Two headless copies keep one world over the real network code and are caught when they do not.
  That holds arm64 against arm64 and arm64 against x86_64, in either slot. `net_check` pins it in
  ctest, with its controls.
- A POSIX LAN lobby now hears broadcasts (#29), shown at the socket level with the real classes.
- Network replays play back in sync against both kinds of recording, and the CRC check stays live to
  the end (F2).
- The argument widths on the wire are pinned for every compiler that builds the file.

**What L1 cannot see:**
- **A Windows peer.** MSVC's code generation, its 32-bit `long` and its CRT are outside this Mac.
  Rosetta is x86-64 under clang, not Windows. E1's `1UL << (dt - 1)` agreed on arm64 and x86_64 and
  differed only on Windows. Only a Windows peer, or a Windows-recorded replay, closes that.
- **Two hosts.** One host's traffic between two addresses never leaves the kernel, so there is no real
  latency, loss, reordering, MTU or switch. Lobby discovery across machines (#29's fix) is shown only
  at the socket level on one host. Linux's socket rules are the same by documentation, but not
  measured here.
- **The firewall.** `net_check` skips while the macOS application firewall is on, rather than put up
  its dialog. So a player's first LAN game on a Mac with the firewall on will see that dialog, which
  nothing here exercises. Third-party filters are not seen either.
- **Also:** NAT and Internet play (out of scope), more than two humans, and the lobby's GUI, map
  transfer and chat.

## Do not

- Do not set SO_REUSEADDR on the unicast game or lobby socket to make two copies share an address.
  BSD then hands each datagram to one of them, and a two-player test can pass while talking to
  itself.
- Do not call an arm64-against-x86_64 pass cross-platform. It is cross-architecture under one
  compiler (E1's wording).
- Do not run the engine on the install. Rule 9: every run uses a farm, and the install is hashed
  before and after.
