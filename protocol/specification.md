# PhoneMirror Protocol v1 — P0 framing contract

Status: the framing layer and local video capture format are implemented. Network authentication, session ordering and control payloads are not yet implemented. Do not expose this parser as a network service without a session/authentication layer.

All integer fields use big-endian byte order. A message is exactly a 40-byte header followed by `payloadLength` bytes. A malformed header is fatal; never scan ahead for a new magic value. Validate the type and length before allocating payload memory.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 4 | magic = `0x504d4952` (`PMIR`) |
| 4 | 2 | version = 1 |
| 6 | 2 | message type |
| 8 | 4 | flags; bit 0 = video keyframe, only allowed for type 11 |
| 12 | 4 | payload length |
| 16 | 4 | stream ID |
| 20 | 4 | stream epoch |
| 24 | 8 | sequence |
| 32 | 8 | presentation timestamp, microseconds in sender monotonic domain |

| Type | Name | Maximum payload |
|---|---|---|
| 1, 2, 3, 4 | HELLO, CAPABILITIES, START, STOP | 64 KiB |
| 10 | VIDEO_CONFIG | 64 KiB |
| 11 | VIDEO_ACCESS_UNIT | 8 MiB |
| 20, 21 | AUDIO_CONFIG, AUDIO_BLOCK | 64 KiB |
| 30 | POINTER_ACTION | 64 KiB |
| 31 | TEXT_COMMIT | 16 KiB |
| 32 | GLOBAL_ACTION | 64 KiB |
| 40, 41 | ACK, STATUS | 64 KiB |
| 50, 51, 52 | PING, PONG, REQUEST_KEYFRAME | 64 KiB |

Unused types and flags are rejected. Payload layouts other than P0 video are reserved and must not be treated as executable commands by the current code.

## Local capture file

The P0 Android app writes concatenated frames using stream ID 1, epoch 1, and increasing sequence numbers starting at 0. There is no file magic beyond each message header. No authentication handshake is stored because this is an app-private offline file, not a network transport.

`VIDEO_CONFIG` payload: width(u32), height(u32), geometryVersion(u32), spsLength(u32), ppsLength(u32), SPS bytes, PPS bytes. Both byte arrays preserve MediaCodec's AVC codec-specific data including start codes. The payload length must equal 20 + spsLength + ppsLength. P0 geometry version is 1; aspect-ratio changes end the probe.

`VIDEO_ACCESS_UNIT` payload: one complete AVC output access unit from Android MediaCodec. Bit 0 indicates a keyframe. Codec configuration buffers are not emitted as access units. Codec PTS is forwarded without pretending it is a wall-clock time.

`pm-inspect` checks framing. The shared `CaptureValidator` used by the player additionally validates configuration lengths, even dimensions up to 4096, SPS/PPS Annex B NAL types, contiguous sequence, stable stream/epoch, initial IDR and nondecreasing timestamps. It limits the timeline to one hour. The offline player accepts one configuration followed by access units; it rejects live stream transitions for now. `pm-playback-check` actually decodes and renders all units, and fails if decoded frame count differs from input. Authentication remains a future transport-layer requirement.

## Shared fixture

`fixtures/ping.hex` is used by both C++ and Kotlin tests. It describes a zero-length PING, stream 0, epoch 1, sequence 1, timestamp 1,000,000 μs. It is independent of host architecture and text encoding.
