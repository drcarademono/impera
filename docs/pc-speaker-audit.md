# PC speaker audit

The previous renderer was not an accurate reproduction of the original effects.
Its unit tests checked a translation of the event arithmetic, but did not compare
timing or the rendered PWM waveform against the original executable.

Reference for this correction: DOSBox 0.74-3, `core=simple`,
`cputype=386_slow`, `cycles=5000`, 48 kHz mixer/speaker. The original calibration
routine returned 623 both in the isolated reference harness and in a memory
dump of the running original game. Physical PCs and other
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

## Sequence timing follow-up

Instrumenting the original DOSBox speaker writes at the profile above measured
49.979 ms for tone (1000, 200), 31.256 ms for pulse (3116, 1, 2000,
20000, -10), and 75.685 ms for sweep (1000, 200, 5, 300). The long casting
pulse (4480, 1, 65000, 300, 1) measured 1016.319 ms; casting noise
(800, 20800, 700) measured 328.892 ms. These are close to the renderer's
primitive durations, so they do not justify a global slowdown. Full spell
sequences, including graphics work and waits between effects, need separate
comparison when a discrepancy is reported.

The original footstep at 433e has a calibrated delay (1, 20) between its
two noise bursts. Smooth movement skipped this delay in native playback,
compressing the pair. Native footsteps now queue the delay as speaker-off
audio, preserving the sound sequence without blocking movement. Recordings
and remapped effects keep their existing dispatch path. An audio integration
test checks the pair duration and verifies that queuing it does not sleep.
