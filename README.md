# Stag Motion Bands

Early JUCE implementation of a multiband modulation plug-in. The current milestone provides:

- VST3, Audio Unit, and Standalone build targets on macOS
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
- Stag brand palette: near-black and ivory surfaces with a single Stag Violet accent
- Per-band LFO or envelope-follower modulation sources
- Adjustable envelope attack and release with a live source activity meter
- Optional per-band LFO phase retrigger from incoming MIDI note-on messages
- Streamlined four-band maximum with left/right arrow-key navigation
- Double-click band creation and double-click divider removal
- Clickable effect sidebar with contextual, effect-specific minimal controls
- Per-band × buttons that remove and merge bands without hunting for a divider
- Per-band Tremolo with the same LFO, envelope, stereo, and mix architecture
- Project Sync mode replaces free-rate Hz with musical note divisions
- Auto Pan, Ring Mod, Phase Warp, and single-sideband Frequency Shifter engines

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

## Test

The processor smoke suite exercises tempo mapping, every effect/waveform combination,
state restoration, and mute behavior:

```sh
cmake --build build --target StagMotionBandsTests -j 4
ctest --test-dir build --output-on-failure
```

When the plug-in is hosted in a DAW, sync mode follows the host playhead tempo. The
standalone target uses a 120 BPM fallback because it has no DAW transport.
