# AutoRoot for Simpler — companion concept

Live/Push Simpler already supplies the constant-duration part with **Warp**. The missing convenience is automatic root correction.

A Max for Live companion therefore should remain small:

1. listen/analyze one played sample note (or an explicitly routed analysis signal),
2. estimate MIDI fundamental + cents,
3. locate the adjacent/target Simpler through LiveAPI,
4. set Simpler Transpose to `60 - detected_midi` (Ableton C3 reference),
5. apply the opposite cents correction to Detune,
6. leave Simpler Warp and all sampling/envelope/filter behavior native.

This folder intentionally does **not** contain a fake finished `.amxd`: Max/Live cannot be instantiated in this environment for validation. The Move/Schwung modules are the implemented part of this package.
