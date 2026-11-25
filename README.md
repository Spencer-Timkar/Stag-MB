# Stag Motion Bands

Early JUCE implementation of a multiband modulation plug-in. The current milestone provides:

- AU, VST3, and Standalone builds on macOS (universal arm64 + x86_64); VST3 and Standalone on Windows and Linux
- An audio-driven FFT spectrum analyzer
- Click-to-create frequency bands
- Draggable crossover dividers
- Double-click crossover removal
- Per-band Solo and Mute controls that affect the audio path
- Custom per-band Chorus, Flanger, and Phaser engines
- Free-running or DAW-tempo-synced modulation with seven musical divisions
- Sine, triangle, saw, and square LFO waveforms
- Selected-band controls for effect type, rate, depth, feedback, wet/dry mix, and stereo spread
- Smoothed modulation parameters to reduce zipper noise during automation
- DAW-automatable and recallable band, modulation, crossover, Solo, and Mute parameters
- Stag brand interface shared with Stag Limiter: violet workspace, cream control deck, Inter type and apricot active states
- Per-band LFO, Envelope, or Transient modulation sources:
  - Envelope follows the band's level in dB; Threshold is the bottom of the swing and
    Threshold + Range the top (defaults -36 dBFS and 18 dB)
  - Transient reacts to hits that jump above the band's recent level, counting only the
    part above Threshold, so hits drive the full swing whatever their level as long as
    Threshold sits below them, while sustained sounds settle back down
  - Attack and Release are time constants of the modulation itself (default 5 ms and
    180 ms), with a live activity meter
- Optional per-band LFO phase retrigger from incoming MIDI note-on messages
- Streamlined four-band maximum with left/right arrow-key navigation
- Double-click band creation and double-click divider removal
- Clickable effect sidebar with contextual, effect-specific minimal controls
- Per-band × buttons that remove and merge bands without hunting for a divider; a band's
  effect and settings travel with it when bands are added or removed, and a new band
  starts with the settings of the band it was split from
- Phase-compensated Linkwitz-Riley crossovers: with every effect off the bands sum flat
- Click-free edits: crossovers glide, Solo/Mute and effect changes ramp, and band-count
  changes fade through silence
- Per-band Tremolo with the same LFO, envelope, stereo, and mix architecture
- Project Sync mode replaces free-rate Hz with musical note divisions
- Auto Pan, Ring Mod, Phase Warp, and single-sideband Frequency Shifter engines; the
  shifter uses an IIR allpass Hilbert pair, so it suppresses the mirror image in low
  bands too

## Build

JUCE 9.0.0 is downloaded by CMake during the first configure.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --target StagMotionBands_Standalone -j 4
```

The standalone app is generated under `build/StagMotionBands_artefacts/Debug/Standalone/`.

Build the plug-in formats with:

```sh
cmake --build build --target StagMotionBands_VST3 StagMotionBands_AU -j 4
```

The project intentionally does not copy plug-ins into system folders after building.

## Platforms and CI

| Platform | Architectures | Formats | Minimum OS |
| --- | --- | --- | --- |
| macOS | Universal (arm64 + x86_64) | AU, VST3, Standalone | macOS 11 |
| Windows | x64 | VST3, Standalone | |
| Linux | x64 | VST3, Standalone | |

macOS universal build (Apple silicon and Intel in one binary):

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release "-DCMAKE_OSX_ARCHITECTURES=arm64;x86_64"
cmake --build build --config Release --parallel --target StagMotionBands_VST3 StagMotionBands_AU StagMotionBands_Standalone
```

Windows x64 (Visual Studio generator, which is multi-config, so pass `--config Release`;
plug-ins link the static MSVC runtime, so no VC++ redistributable is needed):

```sh
cmake -S . -B build -A x64
cmake --build build --config Release --parallel --target StagMotionBands_VST3 StagMotionBands_Standalone
```

Linux x64 (Debian/Ubuntu packages; WebKit and GTK are not needed):

```sh
sudo apt-get install build-essential pkg-config libasound2-dev libfreetype-dev \
  libfontconfig1-dev libx11-dev libxcomposite-dev libxcursor-dev libxext-dev \
  libxinerama-dev libxrandr-dev libxrender-dev libxi-dev libgl1-mesa-dev libcurl4-openssl-dev
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel --target StagMotionBands_VST3 StagMotionBands_Standalone
```

Every platform writes its plug-ins to `build/StagMotionBands_artefacts/Release/`.

GitHub Actions (`.github/workflows/build.yml`) builds and runs the smoke tests on every
push and pull request for macOS universal, macOS Intel (native x86_64 test run), Windows
x64 and Linux x64 (tests under `xvfb-run`). The macOS job also checks with `lipo` that
every binary contains both architectures. The plug-ins are uploaded as the workflow
artifacts `StagMotionBands-macOS-universal`, `StagMotionBands-Windows-x64` and
`StagMotionBands-Linux-x64`. Extract the `.tar.gz` inside the download to preserve
executable permissions. These builds are unsigned and not notarised, so expect
Gatekeeper warnings on macOS (remove the quarantine attribute or allow the plug-in in
System Settings) and SmartScreen warnings on Windows.

## Test licensing

Default builds and public CI artifacts without a configured catalog are developer
builds, not licensed customer downloads. To build an activation-enabled test
plug-in, use the Motion Bands **test** catalog IDs (not Limiter's):

```sh
cmake -S . -B cmake-build-licensed -DCMAKE_BUILD_TYPE=Release \
  -DSTAG_LICENSING_ENABLED=ON -DSTAG_LICENSE_ENVIRONMENT=test \
  -DSTAG_LICENSE_STORE_ID="$STORE_ID" -DSTAG_LICENSE_PRODUCT_ID="$PRODUCT_ID" \
  -DSTAG_LICENSE_VARIANT_ID="$VARIANT_ID"
cmake --build cmake-build-licensed --config Release --parallel
```

Use the architecture or generator options above for the target platform. Setting
the three `STAG_LICENSE_*_ID` GitHub repository variables opts the workflow into
licensed test artifacts. These IDs are public metadata, not credentials.

Copy the test key from **My tools**, open **License…**, and activate online once.
The same OS user shares one slot across plug-in formats; a key has three slots
total across operating systems. Activated builds reopen offline. Deactivate
online to reclaim a slot. Already activated offline installations cannot see a
refund or a remote reset.

[Lemon Squeezy is winding down](https://www.lemonsqueezy.com/blog/saying-goodbye-to-lemon-squeezy).
Do not launch live sales with this test integration. Choose and verify a replacement
activation path before shipping a commercial build.

## Test

The processor smoke suite exercises tempo mapping, every effect/waveform combination,
state restoration, mute behavior, band settings following add/remove, a flat crossover
sum, host blocks larger than announced, click-free edits, frequency-shifter image
rejection, synced LFOs following the host position, the envelope window and release
timing, a steady envelope on low tones, transient response to hits,
a tail length that covers the longest feedback ring, and dragging the right crossover
when automation has left them unsorted:

```sh
cmake --build build --config Release --target StagMotionBandsTests StagMotionBandsLicenseTests -j 4
ctest --test-dir build -C Release --output-on-failure
```

When the plug-in is hosted in a DAW, sync mode follows the host playhead tempo, and while
the transport plays a synced LFO is locked to the host's beat position (unless MIDI
retrigger is on, which then sets the phase). The standalone target uses a 120 BPM
fallback because it has no DAW transport.
