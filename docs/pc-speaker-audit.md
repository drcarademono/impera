# PC speaker audit

The previous renderer was not an accurate reproduction of the original effects.
Its unit tests checked a translation of the event arithmetic, but did not compare
timing or the rendered PWM waveform against the original executable.

Reference for this correction: DOSBox 0.74-3, `core=simple`,
`cputype=386_slow`, `cycles=5000`, 48 kHz mixer/speaker. The original calibration
routine returned 623 in the isolated reference harness. Physical PCs and other
DOSBox core/cycle settings can differ; this is a stable reference profile, not a
claim of cycle-exact sound on every machine or a model of every speaker enclosure.

## Original routines checked

Addresses below are offsets in the original DOS executable's load image.

- `11b4`: timer calibration, measured against BIOS ticks.
- `20c8`: calibrated delay. Tone/sweep use the full calibration.
- `2192`: pulse/PWM. Uses calibration / 24, 16-bit wrapping phase and threshold,
  divisor 60, and both port 61h speaker bits. Pulse parameters are phase increments,
  not frequencies in Hz. Gate transitions and average pin level matter.
- `223c`: noise. Uses calibration >> 4 and its **own** unmasked PRNG at DS:545c,
  initially 7664h. It must not use DS:5420, the general random state.
- `22c0` / `22e2` / `230e`: tone, speaker on, speaker off.
- `43ae`: sweep. Keeps signed, truncated 16-bit arithmetic.

For noise (1, 5, 1000), initial state 7664h, the original writes PIT divisors
0830h, 1132h, 04d3h, 099ch, 0578h. These were also verified by executing the
original instructions in an isolated 16-bit emulator.

A tone (1000, 200) is approximately 50 ms in this profile; the previous renderer
produced 10 ms. A clock pulse (3116, 1, 2000, 20000, -10) has an audible fundamental
near 3 kHz in the DOSBox capture; the old uniform 50 us loop and bipolar carrier
could not reproduce it. Tests now check clip duration and the audible spectrum,
not just whether a nonzero buffer was returned.

## Rendering and playback

The four primitives share the runtime/Workshop renderer. It uses distinct delay
paths, a unipolar speaker pin, proper mode-3 pending counter loads at half-cycle
boundaries, and gate restart behavior. Point sampling the gated high-frequency
carrier aliases into the audible range, so rendering oversamples eight times,
then uses a windowed-sinc low-pass decimator. A DC blocker models AC coupling;
there is no invented per-effect attack/decay envelope or pitch clamp.

Consecutive calls retain fractional samples, filter history, and independent
noise state. Runtime pacing derives from these same event durations rather than
the previous unrelated pulse/noise/tone/sweep sleep approximations. Short
footsteps remain asynchronous to preserve smooth movement. Direct speaker on/off
calls, previously silent stubs, now also reach the renderer.

Title/subtitle reveal slots remain recording-only: there is no authored primitive
recipe for those graphics-driver effects. This audit does not invent one.

## Reproduce the original reference

Run `scripts/speaker-reference.py --game "path/to/Ultima 5" --output /tmp/speaker-reference`,
then DOSBox with `-conf /tmp/speaker-reference/speaker.conf`. The script builds a
small local harness from the supplied executable/data files and runs their original
calibration and speaker routines. Ctrl-F6 starts/stops DOSBox WAV recording.
Generated harness files contain original game code and are not repository assets.
