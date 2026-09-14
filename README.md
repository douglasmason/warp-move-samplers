# Warp Move Samplers — v0.2.0

Four Schwung sound generators for cached pitch shifting with independent sample
duration. Install using this repository in Schwung Manager:

https://github.com/douglasmason/warp-move-samplers

| Module ID | Instrument |
| --- | --- |
| `warpmelodic` | Melodic Sampler+ |
| `warpmrsample` | WarpMrSample |
| `warpdrumkit` | Drum Kit+ |
| `warpmrdrums` | WarpMrDrums |

The two melodic presentations share an engine, as do the two drum presentations.
These are additional Schwung instruments, with stock-like control layouts. They
are not replacements for Ableton's native devices or complete MrSample/MrDrums
feature ports. Movy can use them through its Schwung instrument integration.

## Start playing

1. Install one of the modules above and select it as a Schwung sound generator.
2. Open **Sample** (melodic) or **Pad** (drums) and choose a WAV. Drum versions
   use **Current Pad** to select the pad whose sample and settings you edit.
3. Wait for **Status** in **Capture** to leave **Preparing**. Automatic root
   detection runs when loading or recording. Correct **Root Note** if needed.
   MIDI 60 is shown as C3 in Ableton's octave convention.
4. With **Warp On**, change **Length %** or **Length ms** independently of pitch.
   The last duration representation edited stays anchored when changing trim.
5. For drums, ordinary notes 36–51 trigger pads 1–16. Enable **Pitched Mode** to
   play the selected pad chromatically. **Mode** selects one-shot or gate;
   **Decay** shapes one-shot decay or gate release (zero disables one-shot decay).
6. **Record** in Capture starts/stops recording from Schwung's current input.
   Capture stops automatically at 30 seconds and saves on the background worker.
   Selecting another pad while recording does not change the recording target.

Auto Root and Default Root are instrument-wide ingest policies. Root, trim,
warp, length, grain, envelope and drum playback controls belong to each sample
or pad. Slice Analyze chooses per-slice analysis versus inheriting the parent's
root. Slice makes equal-length slices starting at the selected pad and wrapping
through the 16 pads. Each slice starts at 100 percent length.

## Preparation and live edits

File I/O, analysis, slicing and Bungee rendering run on a low-priority worker.
Playback callbacks enqueue bounded requests and read immutable audio; a held
note keeps its original buffer and playback settings during edits.

The melodic instrument prepares root ±12 semitones in the background. Drums
prepare each pad's root, and the selected pad's ±12 range in Pitched Mode. If a
note has no prepared render, it sounds immediately using conventional sample-rate
transposition while its warped version is requested. Subsequent triggers use the
prepared audio. The first uncached note therefore may have a different duration;
it is never delayed and replayed later. Watch **Preparing**, especially after
changing length, root, trim or tuning.

Audio sources and cached renders share a 64 MiB budget across up to four instances
of a loaded DSP module. Unused cached pitches are evicted to make room; held notes
retain their buffers. Recording buffers have a separate fixed bound. WAV input
is limited to 30 seconds and 12 MiB decoded audio, and an individual warped render
to 12 MiB. If pinned audio prevents preparation, an error is reported and notes
continue with available source playback. Shorten Length or release held notes.

Grain Size uses three Bungee density bands: below 25 ms, 25–90 ms, and above 90 ms.
It is not a continuous millisecond grain-size control.

## Saving

Both engines serialize and restore all their settings, sample paths and length
anchors. Recorded and sliced audio uses unique filenames under each module's
`recordings/` and `slices/` directories. Keep those directories and externally
loaded WAVs when backing up or transferring a Set; the state references the files
rather than embedding their audio. Move's native instrument/preset file format is
not imported by these modules.

## Build and tests

- `./tests/run.sh`: core, host ABI, all four UIs, save/restore, sanitizer,
  callback allocation, live-edit, recording and constrained-memory tests.
- `./scripts/fetch_bungee.sh`: pinned Bungee source and submodules.
- `./tests/run_production.sh`: production Bungee pitch, duration, stereo, tail,
  callback, recording and save/restore checks.
- `./scripts/build_move.sh`: ARM64 modules using Docker or `CROSS_PREFIX`.
- `./scripts/install_move.sh`: optional SSH installation to `move.local`.

Builds automatically fetch the checksum-pinned nlohmann/json header. Release CI
runs both test suites and cross-compiles all four module assets before publishing.

## Validation limits

v0.2.0 is a release for hardware testing. Automated tests and ARM64 builds do not
verify the actual Move audio deadline, hardware Sample-button delivery, native
16-Pitches interaction, Set integration or Movy coexistence on a device. The
explicit Record and Pitched Mode controls are available for testing these paths.
See [AUDIT.md](AUDIT.md) for the original findings and their disposition.
