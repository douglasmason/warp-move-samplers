# Warp Move Samplers — v0.1.1 hardware-test candidate

Four Schwung sound-generator shells sharing two DSP implementations:

1. **WarpMrSample** — MrSample-inspired chromatic single-sample instrument.
2. **Melodic Sampler+** (`warpmelodic`) — same engine, control order/name intended to feel familiar to stock Move Melodic Sampler users.
3. **WarpMrDrums** — 16-pad sampler with independent per-pad warp state.
4. **Drum Kit+** (`warpdrumkit`) — same drum engine, stock-like control order plus an extra Warp bank.

The goal is the workflow discussed in the chat: record/load one pitched sound, determine/store its root, then play pitch-mapped notes/chords whose cached renders all have the **same requested duration**.

## Install with Schwung Manager

This repository is prepared as a **multi-module release**. In Schwung Manager, add the GitHub repository and select the desired module ID:

- `warpmrsample` — WarpMrSample
- `warpmelodic` — Melodic Sampler+
- `warpmrdrums` — WarpMrDrums
- `warpdrumkit` — Drum Kit+

`release.json` points each ID to its own ARM64 release asset.

### Realtime cache behavior

Loading or recording a pitched sample now pre-renders the normal **root ±12 semitone** playing range before the module reports Ready. Entering drum Pitched Mode also prepares that range for the selected pad. This keeps ordinary note-on events inside the prepared range from doing Bungee rendering work in the realtime MIDI path. Notes outside that range are still rendered lazily in v0.1.1; hardware testing will tell us whether to expand the prepared range or move cache generation to a worker.

The test runner now executes the pitch/duration warp invariant test as well as the host ABI tests.

## Parameter model

### Instrument-level ingest policy

- **Auto Root** — applied when a new recording/file arrives.
- **Default Root** — used when Auto Root is off.
- **Slice Analyze** (drum versions) — analyze each new slice independently when on; otherwise inherit the parent root.

After ingest, the root becomes ordinary **sample/pad metadata**. Auto Root is not a per-pad live switch.

### Per sample / per pad playback state

- **Warp** — default **On**.
- **Length %** — default **100% of the trimmed region**.
- **Length ms** — linked bidirectionally with Length %.
- **Grain Size** — default **45 ms**.
- **Root Note** — stored root; may be manually corrected.
- Trim start/end.

The duration representation edited most recently is the hidden anchor. Example: if `%` was last edited, changing the trim preserves the percentage and recalculates ms. If `ms` was last edited, trim changes preserve absolute duration and recalculate `%`.

## Cached playback

With Warp on, playback requests a pitch-specific cached render. The cache key is effectively:

`source + trim + root + requested MIDI note + transpose/fine + target duration + grain setting`

Changing any of those invalidates the affected sample/pad cache. Envelope, gain, pan, choke and similar playback controls do **not** require rerendering.

With Warp off, the instruments bypass the cache and use conventional sample-rate transposition, so higher notes get shorter and lower notes get longer.

## Root-note naming

The UI/code uses Ableton's octave convention: **MIDI 60 = C3**, so A440 / MIDI 69 displays as **A3**.

The current public Schwung host ABI does not expose Move's track key/scale root to a sound-generator plugin. Therefore v0.1 cannot automatically initialize `Default Root` from the current Move input key; it defaults to **C3 (MIDI 60)**. The parameter is exposed so it can be set to the track key root manually. If Schwung adds key/scale context to the host API, this is a small hook to add.

## Recording

Both engines can capture Move's stereo input mailbox. The `Record` parameter toggles capture. The plugins also listen for the internal Move control event using code `118`, matching Schwung's documented Sample/Record control mapping; this hardware-button path still needs on-device verification.

A finished recording is persisted under the module's `recordings/` directory, root-analyzed according to the instrument policy, and becomes the current sample/pad.

## Slicing (drum versions)

`Slice Count` creates equal regions from the currently selected pad's trimmed source and spreads them across pads starting at that pad. Each slice is persisted as WAV. With `Slice Analyze=On`, each gets its own detected root; otherwise it inherits the parent's root. Warp defaults and Grain Size are copied from the parent, and each new slice starts at Length=100% of its own region.

## Grain Size and Bungee

The **production Move build uses Bungee Basic**, the same engine already used by Schwung's `stretch` module. Bungee exposes `log2SynthesisHopAdjust` rather than an arbitrary millisecond grain length, so v0.1 maps the Grain Size knob into three HQ bands:

- `< 25 ms` → denser/smaller grains (`-1`)
- `25–90 ms` → normal (`0`)
- `> 90 ms` → larger/sparser grains (`+1`)

Thus the parameter is exposed and cache-invalidating, but it is **three effective Bungee grain-density regions**, not continuously variable milliseconds yet. A future custom renderer could make this continuous.

The repository contains a dependency-free fallback renderer solely so host/API tests can run without downloading Bungee. **Do not judge production pitch quality from the fallback.** `build_move.sh` always compiles with Bungee and refuses to build without the vendored Bungee tree.

## Build

### 1. Run local host/API tests

```bash
./tests/run.sh
```

### 2. Vendor the pinned Bungee source + submodules

```bash
./scripts/fetch_bungee.sh
```

Pinned commit: `7354c0c62652dd85af90fddfeec307881f3b4252`.

### 3. Cross-build all four modules for Move

Requires Docker:

```bash
./scripts/build_move.sh
```

The script uses Debian Bookworm + GCC 12 aarch64 cross tools and follows Schwung Stretch's Bungee/pffft build flags. Successful output appears under `dist/`:

- `warpmrsample-module.tar.gz`
- `warpmelodic-module.tar.gz`
- `warpmrdrums-module.tar.gz`
- `warpdrumkit-module.tar.gz`

The two melodic shells share one DSP binary; the two drum shells share the other.

### 4. Install over SSH

```bash
./scripts/install_move.sh
```

Target path:

`/data/UserData/schwung/modules/sound_generators/<module-id>/`

Restart/reload Schwung afterward.

## What is implemented in v0.1.1

- cached constant-duration architecture
- Bungee production renderer hook/build
- automatic fundamental detection on ingest
- editable stored root note
- Warp On/Off
- linked Length % / ms
- per-sample/pad Grain Size
- direct input recording
- 16 independent drum pads
- per-pad volume, pan, tune, trim, attack/decay, choke, gate/one-shot and chance
- drum pitched mode for the selected pad
- equal-region slicing with analyze-each vs inherit-root policy
- host ABI smoke tests

## Known parity gaps before calling these drop-in replacements

This is a **working architectural prototype, not full MrSample/MrDrums parity yet**. In particular:

- MrSample's full loop/filter/LFO/AHDSR surface is not all ported yet.
- MrDrums native `.ablpreset` import and its random-pan/random-volume/random-decay/humanize controls are not ported yet.
- The stock-like variants are **Schwung UI/control-order approximations**; Schwung does not provide a public API for literally reusing Ableton's native device skin.
- The Move hardware Sample-button event and stock 16-Pitches interaction need an on-device test; the explicit `Record` and `Pitched Mode` parameters are present regardless.

Those are the next compatibility pass; the new root/warp/length/cache model is isolated so they can be added without redesigning it.

## Safety / recovery

These are unofficial Schwung modules and are not endorsed by Ableton. Keep backups of Sets/samples and install alongside—not over—the stock instruments. The module IDs are unique, so they do not replace native Move devices.
