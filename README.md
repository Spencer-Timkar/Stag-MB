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
- Stag brand interface shared with Stag Limiter: violet workspace, cream control deck, Inter type and apricot active states
- Per-band LFO or envelope-follower modulation sources; the follower works in dB
  (-48 dBFS to 0 dBFS spans the full modulation range), so quiet bands still move it
- Adjustable envelope attack and release with a live source activity meter
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

## Test

The processor smoke suite exercises tempo mapping, every effect/waveform combination,
state restoration, mute behavior, band settings following add/remove, a flat crossover
sum, host blocks larger than announced, click-free edits, frequency-shifter image
rejection, synced LFOs following the host position, envelope response to quiet bands,
a tail length that covers the longest feedback ring, and dragging the right crossover
when automation has left them unsorted:

```sh
cmake --build build --target StagMotionBandsTests -j 4
ctest --test-dir build --output-on-failure
```

When the plug-in is hosted in a DAW, sync mode follows the host playhead tempo, and while
the transport plays a synced LFO is locked to the host's beat position (unless MIDI
retrigger is on, which then sets the phase). The standalone target uses a 120 BPM
fallback because it has no DAW transport.
