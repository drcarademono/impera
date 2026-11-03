# PC speaker timing and rendering

The runtime and Workshop previews/exports share the original DOS speaker
recipes and a common renderer. The timing reference is DOSBox 0.74-3,
`core=simple`, `cputype=386_slow`, **cycles=1000**, 48 kHz mixer/speaker.
This is a reproducible slower-machine profile, not a claim that DOSBox cycles
are physical MHz or that every PC in 1988 sounded identical.

## Why the previous baseline was too fast

The initial audit used 5000 cycles. Its short primitive captures agreed with
Impera, but this reproduced a fast-machine failure in the original game.
Calibration at load-image 11b4 increments a **16-bit** counter at DS:5356
between BIOS timer interrupts (11e5), then multiplies by 18 and divides by
750 (1201–120b). The counter wraps at sufficiently high CPU budgets.

Instrumenting the original executable's speaker writes produced:

| DOSBox cycles | Calibration | Casting pulse (4480, 1, 65000, 300, 1) |
| --- | --- | --- |
| 500 | 219 | 5.123 seconds |
| 1000 | 438 | 3.726 seconds |
| 1500 | 658 | 3.347 seconds |
| 2000 | 878 | 3.093 seconds |
| 3000 | 1317 | 2.882 seconds |
| 5000 | 623 (wrapped counter) | 1.016 seconds |

A memory dump also confirmed calibration 623 in the running original game
at 5000 cycles. Matching that value was insufficient evidence of intended
slower-machine timing. The corrected baseline uses the nonwrapped 1000-cycle
profile. The Sound Effect Speed slider remains an additional playback adjustment;
existing saved choices still apply. Native timing changes leave the PIT tone
frequencies intact, unlike simply resampling all audio at a lower rate.

## Original routines checked

Addresses are offsets in the original DOS executable's load image.

- `11b4`: BIOS-tick CPU calibration.
- `20c8`: calibrated delay. Tone/sweep use the full calibration.
- `2192`: pulse/PWM. Uses calibration / 24, 16-bit wrapping phase and threshold,
  divisor 60, and both port 61h speaker bits. Pulse parameters are phase increments,
  not frequencies in Hz. At 1000 cycles, DOSBox's extra I/O delay truncates to zero.
- `223c`: noise. Uses calibration >> 4 and its own unmasked PRNG at DS:545c,
  initially 7664h, separate from the gameplay PRNG at DS:5420.
- `22c0` / `22e2` / `230e`: tone, speaker on, speaker off.
- `433e`: two noise bursts with delay (1, 20) between them.
- `43ae`: sweep, with signed, truncated 16-bit arithmetic.

Original speaker-write trace durations at the corrected reference:

| Routine and arguments | Duration |
| --- | --- |
| Tone (1000, 200) | 175.993 ms |
| Pulse (3116, 1, 2000, 20000, -10) | 114.427 ms |
| Casting noise (800, 20800, 700) | 1187.408 ms |
| Sweep (1000, 200, 5, 300) | 266.796 ms |

Tests check duration, PWM spectrum, and register sequences. For noise (1, 5,
1000), initial state 7664h, the original writes PIT divisors 0830h, 1132h,
04d3h, 099ch, 0578h; these also match isolated execution of the original code.

## Rendering and playback

The renderer uses separate delay paths, a unipolar speaker pin, mode-3 pending
counter loads at half-cycle boundaries, and gate restart behavior. It oversamples
eight times and uses a windowed-sinc low-pass decimator to avoid audible carrier
aliasing. A DC blocker models AC coupling. There is no per-effect attack/decay
envelope or pitch clamp.

Consecutive calls retain fractional samples, filter history, and independent
noise state. Runtime waits use the same durations as rendering and account for
the player's sound speed. Native footsteps queue their inter-click delay as
speaker-off audio rather than pausing smooth movement. Recordings and remapped
effects retain their existing dispatch path. Direct speaker on/off calls also
reach the renderer.

Title/subtitle reveal slots remain recording-only; there is no authored primitive
recipe for those graphics-driver effects.

## Reproduce the original reference

Run `scripts/speaker-reference.py --game "path/to/Ultima 5" --output /tmp/speaker-reference`,
then DOSBox with `-conf /tmp/speaker-reference/speaker.conf`. The script builds
local harnesses from the supplied executable/data files and executes their
original calibration and speaker routines, including long casting effects.
Ctrl-F6 starts/stops DOSBox WAV recording. Generated harnesses contain original
game code and are not repository assets.
