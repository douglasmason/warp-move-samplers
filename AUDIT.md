# Warp sampler implementation audit

Reviewed against repository commit `25fedf95f941598d852d7350adc9170af7319cba`
and the available Schwung host API and module documentation.

## Readiness

This is an experimental prototype. It is not ready for uninterrupted on-device
performance. The repository had advanced to v0.1.3, beyond the saved v0.1.1
source archive. The melodic UI and parameter getters were already improved in
that repository version; this branch preserves those changes.

## Corrected here

- Bungee preroll output was appended without checking output source positions.
  The zero-transpose 440 Hz test reported 744 positive crossings/second with
  the production renderer, while the fallback passed. Skip invalid/preroll
  output and drain the renderer through the requested output duration.
- Drum gate note-off matched the rendered root instead of the incoming MIDI
  note. Track the incoming note independently of the rendered pitch.
- Drum tuning did not reach the warp renderer. Transfer whole semitones and
  fractional cents into the cache configuration.
- Drum CC 120/123 now stops active voices.
- Sample control handling now accepts internal CC 118 presses, not musical
  note 118 or its note-on-zero release representation.
- Bound drum capture to 30 seconds and reserve its buffer before capture.
  Remember the pad selected at recording start. At the limit capture stops
  appending; the Record action still finalizes it.
- Cleared cache buffers now retire voices instead of leaving active empty voices.
- New samples start root detection from the configured default, so uncertain
  detection cannot inherit the previous sample's root.
- Slices reset duration to 100 percent even when the parent was anchored in ms;
  preserve the parent's tuning in the new cache configuration.
- Reject truncated WAV chunks and inconsistent block alignment before reading
  samples; previously malformed alignment could index outside the input buffer.
- Cache hits verify output sample rate.
- Drum UI metadata is served through DSP `get_param`, with typed controls,
  readable global settings, and write-only action descriptors. The original
  manifest-only declaration was not read by Schwung sound generators.

## Validation

`tests/run.sh` runs the original fallback/core/ABI tests, reads UI metadata and
controls from all four DSP shells, and executes melodic/drum behavior regressions
under AddressSanitizer and UndefinedBehaviorSanitizer. In the managed local
runtime LeakSanitizer cannot enumerate processes, so use
`ASAN_OPTIONS=detect_leaks=0 ./tests/run.sh` there; this does not disable ASan or
UBSan and is not applied to CI.

`tests/run_production.sh` builds the pinned Bungee dependency, runs the original
pitch/duration invariant against it, and adds 54 stereo renders (108 channel
checks): 44.1/48 kHz, half/original/double duration, -12/0/+12 semitones,
and all three effective grain bands. Checks include pitch, finite samples,
non-silent tails, stereo content and output-rate cache changes. These synthetic
checks do not establish musical sound quality for arbitrary recordings.

The PR workflow also cross-builds ARM64 modules. The publishing workflow now
requires production-renderer tests. A cross-build is not on-device validation.

## Outstanding release blockers

1. **Audio-thread work:** Schwung calls all plugin callbacks on the audio thread.
   File loading/writing, pitch analysis, prewarming and cache misses currently
   allocate and do unbounded work there. Moving prewarming into `set_param`
   does not solve this. Use a low-priority background worker, bounded request
   queues and immutable results adopted at block boundaries; retire old audio
   on the worker. Define uncached-note behavior explicitly without delaying
   note events into a different musical position.
2. **Memory budget:** the 128-note caches are unbounded. Prewarming 25 pitches
   for one 30-second stereo sample alone uses about 265 MB of float audio;
   repeating this for 16 pads is untenable. Add a shared byte budget and eviction
   which respects active voices, instead of increasing the prewarm range.
3. **Persistence:** drum state does not serialize, melodic state is not restored
   by `set_param("state")`, and `json_defaults` is ignored. Melodic JSON does not
   escape sample paths or preserve the length anchor. Fixed recording/slice
   filenames can overwrite audio used by other instances. Need unique assets
   and save/reload tests with multiple instances, paths and length anchors.
4. **Live edits:** voices still reference mutable source/cache buffers. Loading
   or editing can silence or change held notes. Freeze voice playback metadata
   and retain immutable buffers until voice completion in the worker redesign.
5. **Functional parity:** drum decay currently acts as gate release, not a
   one-shot decay envelope. Full envelope/filter/LFO/loop controls, preset import,
   root detection on realistic material, and file-browser/recording UX remain.
6. **Hardware integration:** Sample-button delivery, stock 16-Pitches behavior,
   memory/CPU bounds, saving/reloading Sets and Movy coexistence need actual
   device verification after the architecture blockers are addressed.

Keep the four presentation IDs over two engines. Complete the shared loading,
cache and persistence foundation before expanding the instrument feature surface.
