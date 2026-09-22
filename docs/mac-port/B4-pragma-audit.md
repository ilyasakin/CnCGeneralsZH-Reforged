# B4 — Pragma audit (recon half)

This is the audit that task [B4](tasks/B4-pragma-audit.md) asks for, and only the audit. No source
file is touched by it. Writing the `static_assert`s needs a macOS build that does not exist yet
(A1), so the implementation half waits; the survey does not, and it is the slow part.

Everything below was measured against the tree at `feature/mac-port` (`4b08cb58`). Where a number
is quoted it was produced by compiling a faithful standalone replica of the struct with
AppleClang on arm64, not by reading the declaration and counting. The replica is reproduced in
[Appendix A](#appendix-a--how-the-numbers-were-measured) so the next person can re-run it.

**There is no Windows machine on this project.** Nothing here has been checked against MSVC. Where
this document says "MSVC and clang agree", that is a claim about documented ABI behaviour and
about the shape of the declaration, not an observation. Each such claim is marked so you can tell
them apart from the measurements. No line is added to `WINDOWS-DEBT.md`, because no code changed.

## The short version

| Item | Count in scope | Verdict |
|:--|--:|:--|
| `#pragma pack` | 16 lines = **8 regions** | 6 live, 2 dead. One is a live cross-platform desync risk. |
| `#pragma comment` | 6 | 5 are inert on this build; 1 is real and already handled in CMake. |
| `#pragma optimize` | 335 | **332 commented out. 3 live, all behind `_DEBUG`/`_INTERNAL`, neither defined in the shipping config.** Nothing for E1 to fear here. |
| `#pragma warning` | 67 | 62 MSVC-form, 3 Watcom-form, 2 commented. 33 of 34 files are under `WWVegas`. |
| `#pragma inline_depth` | 1 | `WWLib/visualc.h:53`. Suppress and ignore. |

The two things worth waking somebody up for are both in section 1: **`LANMessage`**, whose on-wire
size is computed from a hardcoded `sizeof(wchar_t) == 2`, and **`MAX_PACKET_SIZE`**, a wire-protocol
constant derived from `sizeof(TransportMessageHeader)` — so a packing disagreement silently moves
the protocol instead of failing the build.

---

## 1. The `#pragma pack` sites

Sixteen `#pragma pack` *lines*, which is eight `push`/`pop` *regions*. The task file's "16 sites"
counts lines. All eight are already in the modern `push`/`pop` form — **there is no bare
`#pragma pack(n)` / `pack()` anywhere in scope**, so the conversion work item in the task's step 1
has nothing to convert. (The only bare form in the repository is
`Tools/Autorun/POINT.h:49`, commented out, and `Tools/` is out of scope and stays Windows.)

Two facts that reframe the task's premise:

- **All eight regions are wire formats. None is a file format.** The task file says "every one of
  them is a struct whose layout is a file format or a wire format"; in practice they are all
  network structs, seven of them in `GameNetwork`. The file formats are covered in
  [section 5](#5-the-file-formats-that-have-no-pragma-pack-and-why-that-is-mostly-fine).
- **Two of the eight regions wrap dead structs.** `CommandPacket` and `ConnectionMessage` are
  referenced nowhere but their own declarations. Do not spend implementation budget asserting
  them; see [section 1.7](#17-commandpacket--dead) and
  [section 1.8](#18-connectionmessage--dead).

Measured layouts, AppleClang 17 / arm64, `WideChar = char16_t` (i.e. post-B1):

| Struct | `sizeof` | Last member | Its `offsetof` |
|:--|--:|:--|--:|
| `TransportMessageHeader` | 6 | `magic` | 4 |
| `TransportMessage` | 1110 | `port` | 1108 |
| `DelayedTransportMessage` | 1114 | `message` | 4 |
| `ManglerData` | 20 | `Padding` | 18 |
| `ManglerMessage` | 30 | `port` | 28 |
| `LANMessage` | 471 | *(union)* | 34 |
| `CommandPacket` | *dead* | — | — |
| `ConnectionMessage` | *dead* | — | — |

### 1.1 `TransportMessageHeader` — `GameNetwork/NetworkDefs.h:56–64`

```c
#pragma pack(push, 1)
struct TransportMessageHeader {
    UnsignedInt   crc;     // packet-level CRC (must be first in packet)
    UnsignedShort magic;   // Magic number identifying Generals packets
};
#pragma pack(pop)
```

- **Form:** `push`/`pop`. ✔
- **Measured:** `sizeof == 6`, `offsetof(magic) == 4`.
- **What serialises it:** `GameNetwork/Transport.cpp`. It is never written on its own — it is the
  first six bytes of every UDP datagram the game sends, via `TransportMessage` (§1.2).
  - `Transport::doSend`, `Transport.cpp:238` — `m_udpsock->Write(&m_outBuffer[i], m_outBuffer[i].length + sizeof(TransportMessageHeader), …)`
  - `Transport::queueSend`, `Transport.cpp:403` — CRC is computed **from `&header.magic` forward**:
    `crc.computeCRC(&(m_outBuffer[i].header.magic), length + sizeof(TransportMessageHeader) - sizeof(UnsignedInt))`
  - `Transport::isGeneralsPacket`, `Transport.cpp:428` — the receive side recomputes the same range
    and compares.
- **Asserts B4 should write:**
  ```cpp
  static_assert(sizeof(TransportMessageHeader) == 6, "wire header is 6 bytes on the wire");
  static_assert(offsetof(TransportMessageHeader, crc)   == 0, "CRC must be first in the packet");
  static_assert(offsetof(TransportMessageHeader, magic) == 4, "CRC covers everything after offset 4");
  ```
  The `offsetof(magic) == 4` assert is the load-bearing one. The CRC range is defined by pointer
  arithmetic off `&header.magic`, so if that member moved, two machines would CRC different byte
  ranges and every packet would be rejected as corrupt. That fails loudly rather than silently,
  which is the good case.

### 1.2 `TransportMessage` — `GameNetwork/NetworkDefs.h:107–116`

```c
#pragma pack(push, 1)
struct TransportMessage {
    TransportMessageHeader header;
    UnsignedByte           data[MAX_PACKET_SIZE];
    Int                    length;   // local only
    UnsignedInt            addr;     // local only
    UnsignedShort          port;     // local only
};
#pragma pack(pop)
```

- **Form:** `push`/`pop`. ✔
- **Measured:** `sizeof == 1110`; `header @ 0`, `data @ 6`, `length @ 1100`, `addr @ 1104`,
  `port @ 1108`.
- **What serialises it:** the same three functions in `Transport.cpp`, plus one consumer.
  - **Send:** `Transport.cpp:238` writes from `&m_outBuffer[i]` — i.e. from the top of the struct —
    for `length + 6` bytes. Only `header` and the used prefix of `data` reach the wire; `length`,
    `addr` and `port` are bookkeeping that happens to sit past the end of what is sent.
  - **Receive:** `Transport::doRecv`, `Transport.cpp:297–321`, reads straight into the top of a
    stack `TransportMessage` for up to `MAX_NETWORK_MESSAGE_LEN` (1100) bytes, then sets
    `incomingMessage.length = len - sizeof(TransportMessageHeader)`.
  - **Encryption:** `encryptBuf`/`decryptBuf` operate on `(unsigned char *)&m_outBuffer[i]` for
    `len + sizeof(TransportMessageHeader)` bytes — again from the top of the struct.
  - **Consumer:** `NetPacket::NetPacket(TransportMessage *)`, `NetPacket.cpp:1995`, copies
    `msg->data` wholesale: `memcpy(m_packet, msg->data, MAX_PACKET_SIZE)`.
- **Asserts B4 should write:**
  ```cpp
  static_assert(offsetof(TransportMessage, header) == 0,
      "the socket reads and writes from the top of the struct");
  static_assert(offsetof(TransportMessage, data) == sizeof(TransportMessageHeader),
      "payload must abut the header with no padding");
  static_assert(offsetof(TransportMessage, data) + MAX_PACKET_SIZE == MAX_NETWORK_MESSAGE_LEN,
      "a full datagram must land exactly in header+data and not one byte further");
  ```
  That third one is the important one and it is not obvious. `doRecv` reads 1100 bytes into a
  struct whose `data` array is 1094 long; it is in bounds only because `6 + 1094 == 1100` exactly.
  Change either the header size or `MAX_UDP_PAYLOAD_SIZE` and the read either truncates or runs
  into `length`.

> ### ⚠ Latent desync: `MAX_PACKET_SIZE` is derived from a packed `sizeof`
>
> `NetworkDefs.h:79` reads:
>
> ```c
> static const Int MAX_PACKET_SIZE = MAX_UDP_PAYLOAD_SIZE - sizeof(TransportMessageHeader);
> ```
>
> `MAX_PACKET_SIZE` is a **wire-protocol constant** — it bounds what a peer may send and it is the
> length validated in `isGeneralsPacket` (`Transport.cpp:423`). It is computed from the `sizeof` of
> a packed struct.
>
> If a compiler ever disagreed about that `sizeof` — `pack(1)` ignored, an `#ifdef` that drops the
> pragma on some path, a future member added without thought — then `MAX_PACKET_SIZE` changes, the
> payload offset changes, and **both builds still compile and still run**. They simply cannot talk
> to each other, and the failure presents as "the Mac can't join" rather than as an error. Without
> `pack(1)` the header would be 8 bytes (4-byte `UnsignedInt` + 2-byte `UnsignedShort` padded to a
> 4-byte alignment), payload would start at offset 8, and every field a Windows peer reads would be
> two bytes out.
>
> AppleClang honours `#pragma pack(push, 1)` with MSVC-compatible semantics and gives 6, which is
> what makes this a *latent* risk rather than a present bug. But it is the one place in the network
> code where a layout disagreement produces no diagnostic at all, and it deserves a comment in the
> source as much as it deserves an assert. **E1 and C2 should both know this constant is
> layout-derived.**

### 1.3 `DelayedTransportMessage` — `GameNetwork/NetworkDefs.h:122–128`

```c
#pragma pack(push, 1)
struct DelayedTransportMessage {
    UnsignedInt      deliveryTime;
    TransportMessage message;
};
#pragma pack(pop)
```

- **Form:** `push`/`pop`. ✔
- **Measured:** `sizeof == 1114`, `offsetof(message) == 4`.
- **What serialises it:** **nothing.** This is the latency-simulation hold buffer,
  `Transport::m_delayedInBuffer` (`Transport.h:79`), and it never reaches a socket. The only bulk
  access is `Transport.cpp:272`, `memcpy(&m_inBuffer[j], &m_delayedInBuffer[i].message, sizeof(TransportMessage))`
  — struct to struct, same compiler, both sides.
- **Verdict:** the `pack(1)` here is inherited by proximity, not required. It is harmless and the
  task says not to delete it, so leave it. An assert on it is decorative; write `sizeof` only, and
  say in the comment that this one is in-memory.

### 1.4 `ManglerData` — `GameNetwork/FirewallHelper.h:85–99`

```c
#pragma pack(push, 1)
// size = 16 bytes
struct ManglerData {
    unsigned int   CRC;
    unsigned short magic;
    unsigned short PacketID;
    unsigned short MyMangledPortNumber;
    unsigned short OriginalPortNumber;
    unsigned char  MyMangledAddress[4];
    unsigned char  NetCommandType;
    unsigned char  BlitzMe;
    unsigned short Padding;
};
```

- **Form:** `push`/`pop` (the `pop` at `:108` closes both this and `ManglerMessage`). ✔
- **Measured:** `sizeof == 20`. Offsets: `CRC @ 0`, `magic @ 4`, `PacketID @ 6`,
  `MyMangledPortNumber @ 8`, `OriginalPortNumber @ 10`, `MyMangledAddress @ 12`,
  `NetCommandType @ 16`, `BlitzMe @ 17`, `Padding @ 18`.
- **The comment is wrong.** It says "size = 16 bytes"; it is 20, and has been since somebody added
  fields without updating the comment. This is not a portability bug — every byte is explicitly
  sized and `pack(1)` removes the padding, so MSVC gives 20 too *(reasoned, not measured)* — but it
  is exactly the kind of stale comment that makes the next reader trust the wrong number. **Fix the
  comment when B4 writes the assert.** The assert makes the comment redundant, which is the point.
- **What serialises it:** `GameNetwork/FirewallHelper.cpp`, the NAT port-mangling probe.
  - **Send:** `FirewallHelper.cpp:389` — `spareSocket->udp->Write((UnsignedByte *)&packet, sizeof(ManglerData), address, MANGLER_PORT)`, where `packet` is a `ManglerMessage`. **This relies on `offsetof(ManglerMessage, data) == 0`** — it writes `sizeof(ManglerData)` bytes from the top of the *outer* struct.
  - **Receive:** `FirewallHelper.cpp:459` — `m_spareSockets[i].udp->Read((unsigned char *)message, sizeof(ManglerData), &addr)`, same trick, `message` is a `ManglerMessage *`.
  - **CRC:** `FirewallHelper.cpp:362` and `:462` — `crc.computeCRC(&(packet.data.magic), sizeof(ManglerData) - sizeof(unsigned int))`. Same "from `magic` forward" idiom as §1.1, so `offsetof(magic) == 4` is load-bearing here too.
  - **Byte order:** `FirewallHelper::byteAdjust`, `FirewallHelper.cpp:417`, is the **only explicit
    endianness handling in the whole audit** — `htonl` on `CRC`, `htons` on `magic`, `PacketID`,
    `MyMangledPortNumber`, `OriginalPortNumber`. arm64 macOS is little-endian like x86 so nothing
    changes, but this struct is correct by construction rather than by luck, which none of the
    others are.
- **Asserts B4 should write:**
  ```cpp
  static_assert(sizeof(ManglerData) == 20, "mangler probe is 20 bytes on the wire");
  static_assert(offsetof(ManglerData, magic)   == 4,  "CRC covers everything after offset 4");
  static_assert(offsetof(ManglerData, Padding) == 18, "last member");
  ```
- **Note for B5/M5:** the mangler protocol talks to EA's NAT-negotiation servers, which have been
  dead since 2014, on the same schedule as GameSpy. This code has to compile and link; it will
  never get a reply. Assert it anyway — it is cheap — but do not spend a day on it.

### 1.5 `ManglerMessage` — `GameNetwork/FirewallHelper.h:100–108`

```c
// size = TransportMessageHeader + ManglerData + 10 bytes = 26 bytes
struct ManglerMessage {
    ManglerData    data;
    int            length;
    unsigned int   ip;
    unsigned short port;
};
#pragma pack(pop)
```

- **Form:** shares §1.4's `push`/`pop`. ✔
- **Measured:** `sizeof == 30`; `data @ 0`, `length @ 20`, `ip @ 24`, `port @ 28`.
- **The comment is wrong twice over** — it says 26, it is 30, and the arithmetic it describes
  (`TransportMessageHeader + ManglerData + 10`) does not match the declaration, which has no
  `TransportMessageHeader` in it at all. Replace it with the assert.
- **What serialises it:** as §1.4 — `FirewallHelper.cpp:389` and `:459` cast a `ManglerMessage *`
  and transfer exactly `sizeof(ManglerData)` bytes from its top. `length`, `ip` and `port` are local
  bookkeeping; `length == 0` is the "slot free" sentinel used by `findEmptyMessage`
  (`FirewallHelper.cpp:408`).
- **Assert B4 should write:**
  ```cpp
  static_assert(offsetof(ManglerMessage, data) == 0,
      "the socket reads and writes ManglerData from the top of ManglerMessage");
  static_assert(sizeof(ManglerMessage) == 30, "");
  ```
  The `offsetof(data) == 0` one is the meaningful assert. The rest of the struct's layout is a
  private matter between the file and itself.

### 1.6 `LANMessage` — `GameNetwork/LANAPI.h:306–433` ⚠ **the one that matters**

The LAN lobby broadcast. A tagged union: an enum discriminant, three name arrays, and a union of
eleven payload shapes.

- **Form:** `push`/`pop`. ✔
- **Measured, `WideChar = char16_t`:** `sizeof == 471`, limit 476. `name @ 4`, `userName @ 30`,
  `hostName @ 32`, union `@ 34`.
- **Measured, `WideChar = wchar_t` under clang (4 bytes):** `sizeof == 536`. **Overflows the 476
  limit by 60 bytes.**
- **What serialises it:** `GameNetwork/LANAPI.cpp` and `LANAPIhandlers.cpp`.
  - **Send:** `LANAPI::sendMessage`, `LANAPI.cpp:185–206`, three call sites (unicast, per-slot,
    broadcast), each `m_transport->queueSend(ip, lobbyPort, (unsigned char *)msg, sizeof(LANMessage))`.
    **The entire struct goes on the wire at full `sizeof`, every time, padding and unused union
    bytes included.** There is no field-by-field serialiser anywhere. `sizeof(LANMessage)` *is* the
    wire format.
  - **Receive:** the sixteen `LANAPI::handle*` functions in `LANAPIhandlers.cpp` each take a
    `LANMessage *` pointed straight at the received buffer and read fields at their natural offsets.
  - **Already asserted, partially:** `LANAPI.h:436` has
    `static_assert(sizeof(LANMessage) <= MAX_LANAPI_PACKET_SIZE, …)`.

> ### ⚠⚠ `m_lanMaxOptionsLength` hardcodes `sizeof(wchar_t) == 2`
>
> `LANAPI.h:50`:
>
> ```c
> static const Int m_lanMaxOptionsLength = MAX_LANAPI_PACKET_SIZE - ( 8 + (g_lanGameNameLength+1)*2 + 4 + (g_lanPlayerNameLength+1)*2
>                                                                     + (g_lanLoginNameLength+1) + (g_lanHostNameLength+1) );
> ```
>
> Those two `*2`s are `sizeof(WideChar)`, written as a literal. `m_lanMaxOptionsLength` then sizes
> `GameInfo.options[]` and `GameOptions.options[]`, the largest arms of the union — so the constant
> that is *supposed* to make the struct fit in a datagram is computed from an assumption about
> `wchar_t` that is false on every non-Windows compiler.
>
> **This is the sharpest edge in the audit, and it is also — unusually — safe**, for one reason:
> the existing `static_assert` at `LANAPI.h:436` turns it into a **compile error**, not a desync.
> A naive Mac build where `WideChar` is still `wchar_t` gets `sizeof(LANMessage) == 536 > 476` and
> refuses to build. Measured, both ways, in Appendix A.
>
> So the sequencing is: **B1 must land before this struct can be reasoned about at all**, and once
> B1 makes `WideChar` a `char16_t`, `sizeof(LANMessage)` returns to 471 and matches what a Windows
> build produces *(reasoned — 471 is measured under clang; the MSVC side is inferred from
> `sizeof(wchar_t) == 2` and identical `pack(1)` semantics, and is unverified)*.
>
> Two things B4 should do about it anyway:
>
> 1. **Tighten the assert from `<=` to `==`.** A wire format needs an exact size, not a bound. `<=`
>    would happily accept a build that is 40 bytes smaller and cannot talk to anyone.
>    ```cpp
>    static_assert(sizeof(LANMessage) == 471, "LAN lobby wire format — do not let this move");
>    static_assert(sizeof(WideChar) == 2, "LANMessage's option-buffer arithmetic assumes it");
>    ```
> 2. **Assert the union arm offsets**, not just the total. The handlers read
>    `msg->GameInfo.gameName` and friends at fixed offsets; a change that kept the total size but
>    moved an arm would pass a `sizeof` assert and still desync.
>    ```cpp
>    static_assert(offsetof(LANMessage, name)     == 4,  "");
>    static_assert(offsetof(LANMessage, userName) == 30, "");
>    static_assert(offsetof(LANMessage, hostName) == 32, "");
>    ```
>
> There is one further thing nobody can check from here: **`LANMessage` carries `WideChar` text on
> the wire and no byte-order or encoding conversion is applied to it.** After B1 the payload is
> UTF-16 code units in host order on both ends, and both ends are little-endian, so it works. It
> works by coincidence of endianness, not by design. Worth a sentence in the header.

### 1.7 `CommandPacket` — `GameNetwork/NetworkDefs.h:92–99` — **dead**

```c
#pragma pack(push, 1)
struct CommandPacket {
    UnsignedInt   m_frame;
    UnsignedShort m_numCommands;
    unsigned char m_commands[numCommandsPerCommandPacket * sizeof(GameMessage)];
};
#pragma pack(pop)
```

This looks like the most important struct in the file — the per-frame command packet, the thing
lockstep is made of. It is not used.

- Every function that touched it in `GameNetwork/NetMessageStream.cpp` is inside a block comment:
  `/**` opens at `NetMessageStream.cpp:161` and the matching `*/` is at `:227`. That swallows
  `ClearCommandPacket`, `AddCommandToPacket` and `GetCommandPacket` whole.
- Proof the commented code is stale rather than temporarily disabled: it refers to
  `commandPacket->header.m_numCommands` (`:189`, `:201`), and `CommandPacket` has no `header`
  member. It has not compiled in a very long time.
- `GetCommandPacket` is still *declared* at `NetworkInterface.h:46` and defined nowhere. Nothing
  calls it.
- What is still live are two statics at `NetMessageStream.cpp:158–159`:
  ```c
  static unsigned char commandBuf[sizeof(CommandPacket)+1];
  static CommandPacket *commandPacket = (CommandPacket *)(commandBuf+1);
  ```
  Nothing reads either. They cost about a kilobyte of `.bss` and one deliberately misaligned
  pointer.
- **The real per-frame path is `NetPacket` / `NetCommandMsg`**, `GameNetwork/NetPacket.cpp`, which
  serialises field by field into `m_packet` with explicit `memcpy`s and no packed struct at all.
  That is the code lockstep actually runs, and it has no `#pragma pack` for B4 to assert on.

**Recommendation:** do not write an assert for this. `sizeof(CommandPacket)` depends on
`sizeof(GameMessage)`, a C++ class with a vtable and pointers, whose size legitimately differs
between MSVC x64 and clang arm64 — so any `sizeof` assert on `CommandPacket` would be asserting a
number that has no meaning and is allowed to differ. Assert nothing; instead note the deadness on
the task and let a separate cleanup commit delete the struct, the two statics and the orphan
declaration. That is not B4's job and should not be smuggled into B4's PR.

> The misaligned `(CommandPacket *)(commandBuf+1)` is worth one line of attention even though it is
> dead: `commandBuf+1` is odd-addressed, and `CommandPacket` has 4- and 2-byte members. On x86 that
> is merely slow; on arm64 it is undefined behaviour that clang at `-O2` is entitled to miscompile,
> because it may assume the pointer is suitably aligned and emit instructions that require it. It
> is harmless *only* because no access ever happens. If anyone revives this code, that line has to
> go first.

### 1.8 `ConnectionMessage` — `GameNetwork/Network.cpp:78–88` — **dead**

```c
#pragma pack(push, 1)
struct ConnectionMessage {
    Int           id;
    NetMessageFlags flags;
    UnsignedByte  data[MAX_PACKET_SIZE];
    time_t        lastSendTime;
    Int           retries;
    Int           length;
};
#pragma pack(pop)
```

`grep -rn ConnectionMessage` over the entire tree returns exactly one hit: this declaration. It is
file-local to `Network.cpp` and unused within it.

- **What serialises it:** nothing.
- **Recommendation:** as §1.7 — note it, assert nothing, leave the deletion to a cleanup commit.
- Had it been live it would have needed care: a packed `time_t` is 8 bytes on both toolchains but
  lands at an odd offset here (`4 + 4 + 1094 = 1102`), and an 8-byte load from an odd address is
  the same arm64 hazard described in §1.7. It is not live. Worth knowing if anyone resurrects the
  old connection layer.

---

## 2. The `#pragma comment` sites

Six in scope, and the honest summary is that **five of them do nothing on this build** and the
sixth was already dealt with before B4 existed. This item is much smaller than the task file
implies.

| # | Site | Directive | Live? | CMake target that needs it |
|:--|:--|:--|:--|:--|
| 1 | `GameEngine/Source/Common/System/GameMemory.cpp:3259` | `lib, "GameEngineDebug"` | `#if defined(_DEBUG)` | none — see below |
| 2 | `GameEngine/Source/Common/System/GameMemory.cpp:3261` | `lib, "GameEngineInternal"` | `#elif defined(_INTERNAL)` | none — `_INTERNAL` is never defined |
| 3 | `GameEngine/Source/Common/System/GameMemory.cpp:3263` | `lib, "GameEngine"` | `#else` — **live in Release** | `gameengine` (self-reference) |
| 4 | `GameEngine/Source/Common/System/GameMemory.cpp:3267` | `linker, "/force:multiple"` | `#ifdef MEMORYPOOL_OVERRIDE_MALLOC` | none unless that macro is set |
| 5 | `GameEngine/Source/Common/System/StackDump.cpp:31` | `linker, "/defaultlib:Dbghelp.lib"` | inside `#if defined(_DEBUG) \|\| defined(_INTERNAL) \|\| defined(IG_DEBUG_STACKTRACE)` | `debug` — **already linked**, `CMakeLists.txt:408` |
| 6 | `GameEngineDevice/Source/W3DDevice/GameClient/W3DGranny.cpp:57` | `lib, "granny2"` | file **is not in the build** | none |

Details worth carrying into the implementation half:

- **#1–#3 are not a link dependency at all.** The comment above them at `GameMemory.cpp:3254`
  explains it: *"This is a trick that is intended to force MSVC to link this file (and thus, these
  definitions of new/delete) ahead of all others."* It is a link-**order** hack that names the
  library the file is already part of. There is no external library to add to `CMakeLists.txt`,
  and no clang equivalent is needed — the ordering problem it solves is an MSVC librarian
  behaviour. The right treatment is to let the pragma wrapper header (item 3) swallow it on clang
  and leave the MSVC path byte-identical.

  The accompanying `static int theLinkTester` at `:3270`, and the `++theLinkTester` in
  `STLSpecialAlloc::allocate`, are the "we do debug checking to ensure that's the case" half of the
  trick. They are inert but harmless.

- **#4 `/force:multiple`** is guarded by `MEMORYPOOL_OVERRIDE_MALLOC`, which is not defined
  anywhere in `CMakeLists.txt`. If it ever is, the clang equivalent is *not* a flag — it is
  `-Wl,-force_load` semantics or nothing, and duplicate-symbol tolerance is not something ld64
  offers in the same shape. Flag it on B6 if that macro is ever switched on; today it is dormant.

- **#5 `Dbghelp.lib`** is the one genuine hidden dependency, and `CMakeLists.txt:408` already has
  `target_link_libraries(debug PUBLIC dbghelp comctl32 ws2_32)`. Nothing to add. On macOS the whole
  enclosing block is Windows-only anyway — C5 replaces it. The pragma should stay, behind the
  wrapper.

- **#6 `granny2`** — `W3DGranny.cpp` does not appear in `CMakeLists.txt` at all and no `granny2`
  library exists in the tree. Granny is commercial middleware EA licensed and the sources shipped
  without it. Dormant; nothing to make explicit. Do not add a CMake dependency on a library that is
  not there.

- **Out of scope but named by the task file:** the task says *"`profile.lib` is called out in
  `CMakeLists.txt` as one of them"*. That pragma lives in `Libraries/Source/profile/profile.h`,
  which is outside B4's stated scope (`GameEngine`, `GameEngineDevice`, `Main`,
  `Libraries/Source/WWVegas`) and is **already handled** — `CMakeLists.txt:413–422` builds a
  `profile` target with a comment explaining exactly this, and `:537` links it. Two more sit in
  `Libraries/Source/debug/` (`debug_except.cpp:32` `comctl32`, `debug_debug.h:38`
  `/include:__DebugIncludeInLink1`); `comctl32` is already on line 408. The `/include:` one forces a
  symbol to survive dead-stripping and has a real clang analogue (`-Wl,-u,<symbol>`) if that path
  is ever built on macOS — **hand that one to B6**, it is the only item here that could bite.

**Net conclusion for the task's step 2:** there is no CMake work to do. The correct B4 deliverable
for this item is the wrapper header entry plus a comment in `CMakeLists.txt` recording that the
audit was done and why each site needs nothing — otherwise the next person re-derives all six.

---

## 3. `#pragma optimize` — the determinism question ✅

**This is the item E1 asked for, and the answer is clean: there is no exposure.**

335 occurrences in scope. Classified by whether the line is commented out:

| | Count |
|:--|--:|
| `//#pragma optimize("", off)` (commented) | 329 |
| `// #pragma optimize("", off)` (commented, spaced) | 3 |
| **Live** | **3** |

**Every one of the 332 commented-out lines is the identical string `#pragma optimize("", off)`**,
with no variation beyond one space. It is boilerplate an EA developer pasted into files to have it
ready when a debugging session needed it, and left disabled. There is no site anywhere in scope
where somebody disabled optimisation and wrote down a reason to do with floating point. The
uniformity is itself the evidence: 332 identical lines carry no information.

### Zero in `WWMath`

```
grep -rn "pragma optimize" Libraries/Source/WWVegas/WWMath  →  0
```

The maths library — `dettrig.h`, the vector and matrix code, everything the simulation's
determinism actually rests on — contains no `#pragma optimize` at all, commented or otherwise.

### The three live sites, all inert in the shipping configuration

**1. `GameEngine/Source/Common/System/Debug.cpp:74`**

```c
#ifdef _INTERNAL
// this should ALWAYS be present
#pragma optimize("", off)
#endif
```

- **`_INTERNAL` is never defined.** `CMakeLists.txt:531` defines only
  `$<$<CONFIG:Debug>:_DEBUG>` and `$<$<NOT:$<CONFIG:Debug>>:_RELEASE>`. `_INTERNAL` appears in no
  `target_compile_definitions` anywhere. The pragma does not exist in any build this project
  produces.
- It is also **unterminated** — there is no matching `optimize("", on)` in the file — so in a
  hypothetical `_INTERNAL` build it would disable optimisation for the remainder of `Debug.cpp`.
- `Debug.cpp` is the logging and assertion plumbing. **No simulation maths, no float expressions
  the simulation depends on.** Even if it were live it would be a determinism non-event.

**2 & 3. `Libraries/Source/WWVegas/WW3D2/aabtree.cpp:732` and `:795`**

```c
#ifdef _DEBUG
#pragma optimize("", off)   // We get an odd error when using optimized in the debug.
// All optimized seems to work.  jba.
#endif
bool AABTreeClass::Cast_Ray_To_Polys(CullNodeStruct * node, RayCollisionTestClass & raytest)
{ … }
#ifdef _DEBUG
#pragma optimize("", on)
#endif
```

This is the only live pair in anything resembling maths code, so it gets the full treatment:

- **Properly balanced** — `off` at `:732`, `on` at `:795`, wrapping exactly one function.
- **`_DEBUG`-only.** `CMakeLists.txt:531` defines `_DEBUG` only for `CONFIG:Debug`. The Release
  build — which `NetworkDefs.h:120` notes is the only configuration this fork ships — never sees
  either pragma. The compiler therefore never changes optimisation level in `aabtree.cpp` in any
  build that matters.
- **The author's own comment disclaims it**: *"All optimized seems to work. jba."* That is a
  developer recording that the workaround was no longer needed and not removing it. It is a
  2003-era MSVC debug-build codegen bug, not a float-reassociation guard. Nothing in the enclosing
  comment or the commit history suggests otherwise.
- **The function is float-heavy geometry** — `Cast_Ray_To_Polys` walks a node's triangles and calls
  `CollisionMath::Collide(raytest.Ray, tri, raytest.Result)`, and where `OPTIMIZE_PLANEEQ_RAM` is
  set it recomputes plane normals with `tri.Compute_Normal()` instead of reading them from the
  mesh. If any `#pragma optimize` in this codebase were load-bearing for float behaviour, it would
  be this one.

  **But it is not load-bearing, for the reason that makes this whole item a non-issue:** MSVC's
  Release build optimises this function too. The pragma only ever disabled optimisation in `_DEBUG`.
  So the reference behaviour — what the Windows Release build does, the thing lockstep parity is
  measured against — is *already* fully-optimised code. clang at `-O2` is therefore being compared
  against optimised MSVC, not against unoptimised MSVC, and the pragma protects nothing that the
  shipping build ever relied on.

### What E1 should take from this

1. **No `#pragma optimize` in scope affects any build configuration this project ships.** 332 are
   commented out, and the 3 live ones are behind `_INTERNAL` (never defined) and `_DEBUG` (Debug
   only). E1 does not need to model MSVC-vs-clang optimisation-level differences arising from
   pragmas, because there are none. **This risk can be closed.**
2. **The float-reassociation concern is real but its source is elsewhere.** It is `-ffp-contract`,
   exactly as `docs/mac-port/README.md` already says — clang will fuse `a*b+c` into an FMA on arm64
   where MSVC will not, and `-ffp-contract=off` is the fix. That is E1's whole job and no pragma
   changes it.
3. **One place to point E1's differential test at, if it wants a target:**
   `AABTreeClass::Cast_Ray_To_Polys` and the `CollisionMath::Collide` path beneath it are the
   densest float arithmetic in the tree that the simulation can reach, and they contain
   multiply-then-add shapes that are precisely what FMA contraction changes. Not because of the
   pragma — because of what the function computes. If `-ffp-contract=off` is ever dropped or
   overridden for one translation unit, this is where the divergence shows up first. Worth an
   explicit case.
4. **A mechanical guard is cheap and worth having.** The existing
   `simulation_uses_no_runtime_trig` test already reads engine sources off disk to enforce a
   determinism property. A sibling that greps for a *live* `#pragma optimize` under
   `GameLogic`/`WWMath`/`WW3D2` and fails if one appears would keep this audit true for free —
   today it would pass trivially, which is the best time to add it.

---

## 4. `#pragma warning` / `#pragma warn` — sizing the wrapper header

67 lines in scope, which decompose as:

| Kind | Count |
|:--|--:|
| `#pragma warning(disable : N)` | 35 |
| `#pragma warning(push, 3)` and variants | 13 |
| `#pragma warning(pop)` | 13 |
| `#pragma warning(default : N)` | 1 |
| Watcom-form `#pragma warning N level` | 3 |
| commented out | 2 |
| **total** | **67** |

**The task file's "67 `#pragma warning` + 3 `#pragma warn`" double-counts.** There is no
`#pragma warn` anywhere in scope — `grep -rn "pragma warn("` returns zero. The "3" are the
Watcom-form lines below, which already match `pragma warning` and are already inside the 67. The
wrapper header has 67 lines to cover, not 70.

**Spelling variance is the real sizing problem, not count.** Those 62 MSVC-form lines are written
40 different ways — `#pragma warning(disable : 4514)`, `#pragma warning (disable : 4514)`,
`#pragma warning(disable:4201)`, `#pragma warning ( disable : 4201 )`, `#pragma warning(push, 3)`,
`#pragma warning (push,3)`, `#pragma warning(push,3)` and so on. Any approach based on textual
substitution has to cope with all of them. This is the argument for the task's own instruction —
suppress `-Wunknown-pragmas` globally and let clang ignore the lot — rather than a macro that has
to be spelled consistently.

**Distinct MSVC warning numbers disabled (23):**
`4018 4056 4097 4100 4127 4146 4201 4244 4284 4355 4503 4505 4511 4512 4514 4530 4702 4706 4710
4711 4725 4786 4788`

Essentially all of these are MSVC-specific noise with no clang counterpart — `4514`/`4710`/`4711`
are inlining chatter, `4786`/`4788` are the VC6 "identifier truncated to 255 characters in the
debug information" pair, `4503` is decorated-name-length, `4530` is "C++ exception handler used
without unwind semantics". Three have a rough clang analogue worth a thought rather than blanket
suppression: **4244** (`-Wconversion`, narrowing), **4018** (`-Wsign-compare`), **4706**
(`-Wparentheses`, assignment within conditional). The rest should map to nothing.

**Where they live — this is the useful number for scoping the wrapper:**

- **34 files hold all 67.** 32 are under `Libraries/Source/WWVegas` (17 in `WWLib`, 6 in `WW3D2`,
  4 in `WWAudio`, and one each in `WWDebug`, `WWMath`, `WWSaveLoad`).
- **Exactly 2 are in `GameEngine`**: `Common/System/List.cpp` and `Common/System/String.cpp`.
- **Zero in `GameEngineDevice` and zero in `Main`.**

So the wrapper header is a `WWVegas` concern almost entirely, and `WWLib/always.h` — which already
carries `#pragma warning` lines of its own and is included nearly everywhere in that tree — is the
natural home. The two `GameEngine` files can include it or be left to the global
`-Wno-unknown-pragmas`.

**The 3 Watcom-form lines**, all in `WWLib` and all pre-dating the MSVC port:

| Site | Line |
|:--|:--|
| `Libraries/Source/WWVegas/WWLib/ini.cpp:117` | `#pragma warning 665 9` |
| `Libraries/Source/WWVegas/WWLib/mpmath.cpp:355` | `#pragma warning 364 9` |
| `Libraries/Source/WWVegas/WWLib/readline.cpp:51` | `#pragma warning 665 9` |

(Two more are commented out at `WWLib/fixed.h:43–44`.) These are Watcom C/C++ `#pragma warning
<number> <level>` syntax, not MSVC's. **MSVC has been silently ignoring them for twenty years** —
they are not valid MSVC spellings and never did anything in this build. clang will ignore them
equally. They need no translation, only suppression, and the "3 `#pragma warn`" entry can be struck
from the task's checklist.

**`#pragma inline_depth(255)`** — one site, `WWLib/visualc.h:53`. Wrapper swallows it. clang has no
equivalent and does not need one; its inliner is not depth-limited in the way VC6's was.

---

## 5. The file formats that have *no* `#pragma pack` — and why that is mostly fine

The task file's premise is that the `#pragma pack` sites are where the file-format risk lives. They
are not: all eight are network structs (§1). Since B4's real purpose is "stop a Mac build silently
misreading a `.w3d`", this section records what was found when that question was asked directly.

**`Libraries/Source/WWVegas/WW3D2/w3d_file.h` has no `#pragma pack` at all**, and its structs are
read with bulk binary reads — e.g. `distlod.cpp:304`,
`cload.Read(&lodStruct, sizeof(W3dLODStruct))`. Layout is load-bearing and nothing asserts it.

This is nonetheless **safe, by EA's design rather than by accident**:

- Every data member in the file is drawn from `uint8` / `uint16` / `uint32` / `sint32` / `float32`
  (the width-pinned typedefs from `bittype.h`), `char` arrays, or nested structs of the same. A
  census of the header gives 115 `uint32`, 63 `float32`, 43 `uint8`, 33 `char`, 21 `uint16`, plus
  nested `W3dVectorStruct` / `W3dRGBStruct` / `W3dRGBAStruct` / `W3dQuaternionStruct`, which are
  themselves made of the same.
- **No `double`, no pointer, no `long`, no `bool`, no `wchar_t`, no enum data member.** The five
  `bool`s in the file are return types on `operator==` / `operator!=`, not members. There is no
  member whose size or alignment differs between MSVC x64 and clang arm64.
- Because the members are naturally aligned at their own widths, the compiler inserts no padding
  it would have to agree about. `W3dChunkHeader` is two `uint32`; `W3dMeshHeader3Struct` opens with
  two `uint32` and two 16-byte `char` arrays. These lay out identically under any sane ABI
  *(reasoned from the Itanium C++ ABI and MSVC's documented default `/Zp8`; not measured against
  MSVC)*.

So the `.w3d` loader should work. **It is unasserted, though, and that is worth fixing while B4 is
in the area** — these structs have a far stronger claim on `static_assert`s than
`DelayedTransportMessage` (§1.3) does, because they are real file formats read in bulk from disk,
and the task's stated goal is exactly that. A handful of asserts on `W3dChunkHeader`,
`W3dMeshHeader3Struct`, `W3dVertexMaterialStruct` and `W3dHierarchyStruct` would cover the common
load path. Adding them is arguably outside B4's literal scope, since B4 is scoped by pragma; the
scope should be widened rather than the structs left bare.

**The `.big` archive reader does not have this exposure.** `Win32BIGFileSystem.cpp:251` builds
`ArchivedFileInfo` field by field from individual reads rather than blitting a struct, so no layout
dependency exists. C1 inherits that path and should keep it field-by-field.

**Save games were not audited here.** They are `C5`/`M2` territory and no `#pragma pack` appears in
`Common/System/SaveGame` (its two `#pragma optimize` hits are both commented out). Whether the save
format blits structs is a question for whoever takes C5; it was not answered by this audit and
should not be assumed answered.

---

## What B4's implementation half should actually do

Revised against what the audit found, in priority order:

1. **Wait for B1.** `LANMessage` cannot be asserted until `WideChar` is `char16_t`; today a Mac
   build fails at `LANAPI.h:436` before it reaches anything B4 would add. B4's `pack` work is
   effectively blocked on B1, not just on A1. **This dependency is not in the README's graph and
   should be added.**
2. **Write asserts for the six live regions** (§1.1–§1.6), preferring `offsetof` over `sizeof`
   wherever the serialiser uses pointer arithmetic — which is most of them. Tighten `LANAPI.h:436`
   from `<=` to `==`.
3. **Skip §1.7 and §1.8.** Note the dead structs on the task; do not assert them; leave deletion to
   a separate commit.
4. **Fix the two stale size comments** in `FirewallHelper.h` (16→20, 26→30) as part of the assert
   commit.
5. **Add the wrapper header** under `WWLib`, sized for 67 lines across 34 files, 32 of them in
   `WWVegas`. Suppress rather than translate.
6. **Do no CMake work for item 2.** Record why in a comment instead. Hand
   `debug_debug.h`'s `/include:` pragma to B6.
7. **Consider widening scope to `w3d_file.h`** (§5), or raise it as its own task. It is where the
   file-format risk the task set out to address actually lives.

---

## Appendix A — how the numbers were measured

Every size and offset above came from compiling a standalone replica of the declarations — same
members, same order, same `#pragma pack(push, 1)`, same derived constants — under AppleClang on
arm64, once with `WideChar = wchar_t` and once with `WideChar = char16_t`:

```
clang++ -std=c++17 -O2 lay.cpp && ./a.out
```

Results, trimmed to the lines that matter:

```
                                   wchar_t (4)      char16_t (2)
sizeof(TransportMessageHeader)           6                6      offsetof(magic) = 4
MAX_PACKET_SIZE                       1094             1094
sizeof(TransportMessage)              1110             1110      data @ 6, length @ 1100
  header + data                       1100             1100      == MAX_NETWORK_MESSAGE_LEN
sizeof(DelayedTransportMessage)       1114             1114      message @ 4
sizeof(ManglerData)                     20               20      Padding @ 18   (comment says 16)
sizeof(ManglerMessage)                  30               30      data @ 0, port @ 28  (comment says 26)
m_lanMaxOptionsLength                  400              400
sizeof(LANMessage)                     536              471      limit 476
                              *** OVERFLOWS ***         OK       name @ 4, userName @ 30, hostName @ 32
```

The `LANMessage` row is the point of the exercise: the same declaration, the same compiler, the
same flags, differing only in the width of `WideChar`, and the wire format moves by 65 bytes.

**What this does not establish.** These are clang numbers. The MSVC column is absent because there
is no Windows machine on this project, and every statement above of the form "MSVC agrees" is
reasoning from the documented ABI, not a measurement. The asserts B4 adds are what will finally
close that gap — they will fail the Windows build if the reasoning was wrong, which is precisely
why they belong in the source and not in this document.
