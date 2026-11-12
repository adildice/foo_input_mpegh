# foo_input_mpegh

MPEG-H 3D Audio decoder for foobar2000. It plays MPEG-H tracks (codec IDs `mha1` and `mhm1`)
stored in MP4 / M4A files, for example 360 Reality Audio music, and makes them readable by
other components such as DR Meter, ReplayGain scanning and the converter.

Decoding is done by the Fraunhofer FDK MPEG-H decoder (mpeghdec); MP4 parsing by Fraunhofer
mmtisobmff.

## Requirements

foobar2000 2.0 or newer for Windows, 32-bit or 64-bit.

## Install

Download `foo_input_mpegh.fb2k-component` from the Releases page and open it (or use
Preferences > Components > Install), then restart foobar2000.

## Settings

Preferences > Advanced > Decoding > MPEG-H 3D Audio decoder:

- Output layout: Stereo (default), 5.1 or 7.1. MPEG-H is object based, so the decoder renders
  to the layout chosen here. The setting applies to the next track opened.
- Target loudness (LKFS) and DRC effect: optional, empty = decoder default.

Measurements (DR Meter, ReplayGain) are taken on the rendered output, so they depend on the
layout and on the loudness / DRC settings.

## Notes and limits

- Files without an MPEG-H track are ignored; foobar2000's own MP4 decoder keeps handling them.
- Tags are read (title, artist, album, album artist, date, genre, composer, comment, track and
  disc numbers) but cannot be edited through this component.
- MPEG-H Baseline / Low Complexity profile, up to level 4. No raw `.mhas` files, no DRM.
- After seeking, playback continues from the exact position; the decoder restarts from a sync
  frame, so levels can differ minimally from uninterrupted playback for a moment.

## Building

Windows, Visual Studio 2022 with CMake:

    cmake -S . -B build -A x64        (or -A Win32)
    cmake --build build --config Release --target foo_input_mpegh

The foobar2000 SDK and the Fraunhofer libraries are downloaded by CMake. The GitHub workflow in
`.github/workflows/build.yml` builds both architectures and packages the `.fb2k-component`.

A host-free self-test (`-DMPEGH_BUILD_SELFTEST=ON`, target `selftest`) builds on any platform
and checks decoding, seeking and rejection of non-MPEG-H files.

## Licence

The component's own code is MIT (see `LICENSE`). It contains the Fraunhofer FDK MPEG-H Software
(mpeghdec), mmtisobmff and ilo, and is built with the foobar2000 SDK; their licence texts are in
`third_party_licenses/`. The Fraunhofer licence grants no patent licence; this component is
free and meant for personal, non-commercial use.
