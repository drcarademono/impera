# Recorded effect playback audit

Audited the 115 vanilla recording slots and their trigger paths in `sfx_map.h`,
`aud_sfx.c`, and the gameplay callers. Repeated triggers do not all represent
continuous ambience; looping every recurring effect would destroy notes, rhythm,
and individual action feedback.

| Recordings | Trigger pattern | Handling |
| --- | --- | --- |
| `watrfall.wav`, `fountain.wav` | Nearest environmental source, checked on map updates | Continuous contextual loops, separate tracks, joined loop seams, source transition fades. |
| `chime.wav`, `tickhigh.wav`, `ticklow.wav` | Clock beats/chimes from ambient updates | Separate finite environmental voices; never an infinite loop. |
| `lute0.wav`–`lute8.wav` | Scheduled bard melody notes | Separate finite voices; repeated pitches still retrigger as new notes. |
| `harpsi0.wav`–`harpsi9.wav` | Interactive instrument notes | Same finite note voices, allowing a preceding note to decay. |
| `drip0.wav`–`drip2.wav` | Discrete dungeon drip animation events | Finite environmental voices; choose the matching 20/12/4-duration recording. Missing variants retain the original fallback. |
| `step0.wav`, `step1.wav`, `dunstep0.wav`–`dunstep3.wav` | Individual movement events | Foreground one-shots; do not turn into continuous loops or suppress legitimate repeat steps. |
| Spell circles, ritual sweeps, Blackthorn/chant/apparition phrases, victory notes | Scripted, finite sequences | Keep authored triggers and existing complete-recording dispatch; apply general boundary smoothing. |
| Damage, attacks, blocked movement, UI/status, transport, world-event, introduction and title effects | Individual actions or finite animations | Foreground one-shots with boundary/transition smoothing. |

No additional unbounded environmental sound bed was found in vanilla callers.
Clock, instrument, and drip recordings nevertheless needed protection from
foreground replacement: the old shared recording track could cut them off as
soon as a footstep or other effect played.

All decoded recordings, including mod replacements, get short linear ramps at
both file boundaries. Ramps are limited to 2 ms and at most one quarter of a
short clip, preserving its duration and interior. This also covers clicks at the
natural end of a recording, which playback-start and replacement fades alone
could not address. Original files and Workshop exports are not rewritten.

The finite environmental pool keeps up to two complete notes plus a brief
outgoing tail. It is bounded, has no infinite loops or unbounded queue, and uses
the Sound Volume and Sound Effect Speed settings. Classification happens before
mod event remapping so a replacement inherits its trigger's playback role.
Sound mute, mod reload, cleanup, and return to the main menu stop these voices.
Looped and finite versions of the same recording have distinct caches.

Regression coverage checks note survival during foreground playback, legitimate
note retriggers, all three drip variants, independent loop playback, file edge
ramps, settings, and cache invalidation. Fixtures include mono/stereo recordings
and the supplied fountain/waterfall files in local verification.
