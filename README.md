# SMG2 reverse-engineering research

This repository snapshot contains a small set of independently written C++20 research modules and tests from a larger Super Mario Galaxy 2 reverse-engineering and porting effort. It is not a complete game, a game source-code release, or a playable Mac/iPhone application.

An [experimental macOS Apple Silicon application preview](https://github.com/alexkypraiou/smg2-reverse-engineering/releases/tag/macos-preview-0.1) is available separately. It requires user-supplied game data and **a normal fresh launch currently stops at a known error**. It is not a verified playable release.

## What is included

- Host-side implementations of selected CubeGravity, PlanetGravity, and PointGravity operations.
- Bounds-checked readers for Yaz0, RARC, U8, BCSV/JMap, Wii FST, and selected J3D container and VTX1 metadata.
- Synthetic-input unit tests for those modules. The standard test suite does not require game files.
- [`PROVENANCE.txt`](PROVENANCE.txt) and [`DATA_PROVENANCE.md`](DATA_PROVENANCE.md), which describe the behavioral references and implementation limits.
- [Runtime integration patches](patches/README.md) for specific ModernGekko and RecompCore revisions, with their upstream license notices.
- Research scripts for a local DOL inventory and independent asset-scan comparisons. They require separately provided local inputs.

The gravity code uses portable host types, not Wii object layouts. It is an independent implementation of selected behavior informed by a hash-matched public reference and binary analysis. Host floating-point operations are not guaranteed to reproduce Wii SDK vector helpers bit for bit. The data readers are parsers, not a full game runtime or renderer; J3D support is partial and does not decode/render complete models.

## Mac and iOS status

The broader local project has reached a partially working native macOS runtime/prologue, using a statically recompiled game module and Dolphin-derived runtime services. The complete runtime, generated game code, service dependencies, assets, and build products are not included here; selected runtime changes are shared as [patches](patches/README.md). A fresh-launch regression has also been observed. The local milestone is not full-game completion or an independently portable game source release. This source snapshot alone does not launch a game.

The iOS work is an incomplete scaffold in the private working tree and is not included. There is no iPhone app, installable IPA, or verified iPhone 13 build. The available local report described a host-side Mac validation only; a full iOS runtime/application, graphics and lifecycle integration, input, audio, sandbox paths, device testing, and signing remain. No iOS capability is claimed by this repository.

## Build and run tests

Requirements: CMake 3.20 or newer and a C++20-capable compiler (Apple Clang or GCC/Clang). From this repository root:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

For Apple Silicon explicitly:

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64
cmake --build build -j4
ctest --test-dir build --output-on-failure
```

The optional `fst_game_data` CTest requires a locally extracted, legally obtained game `DATA` directory and can be enabled with `-DSMG2_DATA_ROOT=/path/to/DATA`. It is intentionally not part of the default tests. The `smg2_asset_scan` executable likewise consumes a user-supplied extracted data directory; it does not download or include one.

## Data, dependencies, and provenance

The source tree includes no Nintendo game image, extracted game data, proprietary game binary, game-derived pseudocode/disassembly, artwork, audio, screenshots, videos, save files, or user data. The separate macOS preview archive contains a game-specific generated module but no game image, extracted game data, checkpoint or personal save. Compatible game data must be supplied separately by the user where lawful.

Third-party dependencies used by the larger private runtime are not vendored here. The included native library/tests use the C++ standard library and CMake. The provenance documents identify the behavioral references used for these modules; in particular, Garigari is a separate public reference project and is not bundled here. Its own license does not automatically license this project's original contributions.

Research references include [Garigari](https://github.com/SMGCommunity/Garigari), [DolRecomp](https://github.com/ExpansionPak/DolRecomp), and [ModernGekko](https://github.com/ExpansionPak/ModernGekko). Each project maintains its own licensing and attribution; this snapshot is not affiliated with or endorsed by Nintendo.

No license has yet been specified for the separately authored native C++ modules, tests, and research scripts. Until the project authors choose one, do not assume these contributions are licensed for reuse, redistribution, or modification. The runtime patches carry the upstream licenses and notices described in [`patches/README.md`](patches/README.md).

## Optional research scripts

`scripts/inventory_dol.py` reads a user-supplied DOL, a matching Garigari
checkout, and an existing `dtk-dol-info.txt` report in its evidence directory.
Pass explicit `--dol`, `--reference`, and `--evidence` paths; run with `--help`
to inspect the options. It creates local inventory and symbol reports.
`scripts/inspect_asset_fixtures.py` and `scripts/verify_asset_scan.py` compare
independent local-data fixtures with scanner output. They likewise require
explicit local inputs and output paths; generated reports and game data are
not included in this repository.

## Contributing

Useful contributions include improving parser bounds/error handling, adding synthetic format fixtures and tests, documenting confirmed format behavior, and extending gravity tests with reproducible host-side cases. Keep proprietary game files, extracted assets, generated game code, decompiler output, and personal data out of proposed changes. Document behavioral references and distinguish measured behavior from inference.
