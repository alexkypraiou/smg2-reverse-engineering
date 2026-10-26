# Native resource formats

The loader operates on bytes from the user's extracted game data. It owns its
buffers, performs big-endian loads explicitly, and does not cast host pointers
to 32-bit Wii object layouts.

BCSV/JMap layout and value access are source-informed by the hash-matched
Garigari reference at commit `c590a376e00a6b5d4f2bc963dfd6f5516080e07d`:
`include/Game/Util/JMapInfo.hpp` and `src/Game/Util/JMapInfo.cpp`. The
DOL assembly was inspected locally and is not included; string, unsigned,
and signed getter entry points are `0x80067790`, `0x80067810`, and `0x800678A0`.
The local JGadget hashcode assembly at `0x8050E4B0` establishes the
multiply-by-31 hash with sign-extended input bytes. The host API checks type
compatibility and input bounds more strictly than the original raw accessors.
Malformed inputs are rejected rather than reproducing Wii memory faults.

Yaz0 and RARC are informed by local JKR archive/decompression code and tested
against real extracted archives. RARC entry flags preserve storage classes
and compression metadata; a stored payload is not automatically assumed to be
an uncompressed file. BCSV strings remain raw bytes, often Shift-JIS. The scan
report escapes high bytes without claiming to convert Japanese text to Unicode.

Independent Python decoders in the local working tree were used to compare
selected archive/table row counts, names and transforms with the native scan.
Those private-data fixture checks are not included in this snapshot. The
included unit tests exercise malformed dimensions, offsets, back-references,
string bounds and scalar field semantics using synthetic inputs.

The Wii FST reader preserves original indices and word-based file positions,
exposing byte offsets with an explicit multiply by four. The unit test can
compare every indexed path and size against the extracted game directory.
It is an index, not an asynchronous DVD device implementation.

J3D support currently covers container/section bounds, proven section metadata
and VTX1 position formats and encoded bytes. It does not yet decode full
display lists, materials, skinning, textures or animation for rendering.
