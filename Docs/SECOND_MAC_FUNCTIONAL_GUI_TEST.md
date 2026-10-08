# Second Mac — Functional GUI Test (ChordEngine Native)

Development build only. Nothing in this document installs, signs, notarizes,
packages or publishes anything.

This revision is the **functional fix pass** for the four issues reported after
the first second-Mac test: A. Fixed Velocity, B. Internal Audio, C. Clickable
Piano, D. Centered Record Arm. Sections E–G are regression checks for the
behaviour that was already verified and must stay working.

## Artifacts under test

| Format | Archive | Purpose |
| --- | --- | --- |
| AU (aumi) | `Build/TestArtifacts/ChordEngine-Native-macOS-AU-Functional-Fix-Test.zip` | Logic Pro (Audio Units MIDI FX) |
| VST3 | `Build/TestArtifacts/ChordEngine-Native-macOS-VST3-Functional-Fix-Test.zip` | VST3 MIDI effect hosts |

Install the test copies with the host's own temporary plugin folder (or copy
them into `~/Library/Audio/Plug-Ins/Components` / `.../VST3` for the duration
of the session). Remove them afterwards — these are unsigned development
builds.

## Ground rules

* Use a **NEW, disposable Logic project**. Do not open or save any existing
  Logic session.
* No Logic automation, no scripted actions. Every step is done by hand.
* Do not modify anything under `HISE Projects/`, the installed FL 0.4.0 VST3,
  or the release DMGs while testing.
* Record the observed result for each line, not just pass/fail.

---

## A. Fixed Velocity (interactive control)

The Fixed Velocity control must be a real JUCE `Slider` that responds to mouse
click and drag. It must never look live while it is inert.

| # | Step | Expect |
| --- | --- | --- |
| A1 | Set **VELOCITY MODE** to **Dynamic**. | The **FIXED VELOCITY** slider is disabled exactly as the reference disables it. It is also **visibly dimmed** (about one third opacity), so "looks live but dead" cannot happen. The readout shows **100**. |
| A2 | Set **VELOCITY MODE** to **Maximum**. | The slider stays dimmed and inert; generated notes are velocity 127. |
| A3 | Set **VELOCITY MODE** to **Fixed**. | The slider becomes **fully opaque** and interactive. |
| A4 | **Drag** the slider thumb left and right. | The thumb follows the mouse and the numeric readout updates live as you drag. |
| A5 | **Click** somewhere on the empty track (do not drag). | The thumb jumps to the clicked position and the readout updates. Clicking is not required to start on the thumb. |
| A6 | Set the slider to **1**, play a chord on the piano keyboard or an attached keyboard. | Every generated note has velocity 1. |
| A7 | Set the slider to **64**, play a chord. | Every generated note has velocity 64. |
| A8 | Set the slider to **100**, play a chord. | Every generated note has velocity 100. |
| A9 | Set the slider to **127**, play a chord. | Every generated note has velocity 127. |
| A10 | Set the slider to a value, then keep playing further chords without touching the slider. | Every subsequent generated chord keeps the same velocity — the value survives further MIDI processing in the session. |
| A11 | Switch back to **Dynamic** and play with different key pressures. | Generated velocity follows the played velocity again. |

## B. Internal Audio (honest limitation)

**Decision for this pass: ChordEngine stays a pure MIDI processor in both
formats.** Internal audio needs an audio output bus, and neither an AU `aumi`
MIDI FX nor a VST3 MIDI effect has one. The card therefore reports the
capability as unavailable rather than pretending to switch it.

| # | Step | Expect |
| --- | --- | --- |
| B1 | Look at the INTERNAL AUDIO card. | The chip reads **N/A** (not `ON`/`OFF`) and is drawn in the disabled palette. The sub-caption reads **MIDI effect - no audio bus**. |
| B2 | Click the chip. | Nothing happens. It is **not a button** and has no click handler, so it can never fake a toggle. |
| B3 | Hover the chip. | A tooltip explains: internal audio needs an audio output bus and ChordEngine is a pure MIDI effect with no audio buses; routing to a downstream instrument is the supported path. |
| B4 | Play chords with the card present. | Chord MIDI still flows to the downstream instrument. Nothing about the MIDI path depends on this card. |
| B5 | Confirm no ON/OFF state ever appears. | Never accept a visually changing switch without real audio behaviour — there is none here, by design. |

**Why B1–B5 are the correct behaviour.** The reference "Internal Audio" drives
the reference product's own Sine Wave Generator with `setBypassed()`. That
requires an audio output bus. This product is an AU `aumi` MIDI processor and a
VST3 MIDI effect, and `isBusesLayoutSupported()` accepts neither an input nor
an output bus — verified by an automated test
(`ChordEngineFunctionalTests`, case 18) that asserts a stereo output bus is
rejected. Giving the VST3 an audio bus would stop it being a MIDI effect and
would change the plugin identity and host routing the product is built around
(it shares the Music-Prod ChordEngine bundle identity), while an audio bus is
impossible for the AU MIDI FX at all. The correct product answer is therefore
an honest, documented limitation — never a fake switch.

## C. Clickable Piano (mini MIDI keyboard)

The C2–C7 keyboard must behave like a MIDI keyboard: each key sends a real note
into the same processor/Core trigger path an external keyboard uses.

| # | Step | Expect |
| --- | --- | --- |
| C1 | Use the **AU** build in Logic's MIDI FX slot with a software instrument downstream. | ChordEngine inserts as before. |
| C2 | **Click and hold** the **C3** key on the on-screen piano (third C from the left). | A note-on is generated for C3, the C3 key highlights as a trigger, a chord is generated and the downstream instrument plays it. |
| C3 | Keep holding C3. | The chord keeps sounding; no note drifts or retriggers. |
| C4 | **Release** the mouse. | The trigger and every generated chord note are released downstream; nothing hangs. |
| C5 | Press **and hold** C3, then **drag** onto E3 while still holding, and release there. | C3 releases, E3 triggers, and the downstream instrument moves to the E3 chord. No stuck notes. |
| C6 | Press and hold a key, then drag **off** the keyboard area (below or above the key bed) and release. | The held note releases cleanly with no hanging chord. |
| C7 | Click white keys across the full width, left to right. | Every white key sounds its own note and lights up; the leftmost is **C2** and the rightmost is **C7**. |
| C8 | Click the black keys. | Each black key sounds its own sharp note (C#, D#, F#, G#, A#) and lights up. |
| C9 | Click a key and view the chord readout. | The chord name/notes shown match what an external keyboard produces for the same key. |
| C10 | Press and hold a piano key, then switch to the INFO page or close the editor. | The note is released — no stranded note-on. |
| C11 | Play the same note on an attached MIDI keyboard. | The generated chord is identical to the on-screen key (the on-screen keyboard never computes its own harmony). |

## D. Centered Record Arm

| # | Step | Expect |
| --- | --- | --- |
| D1 | Look at the CHORD page. | The **RECORD ARM** pill is visually **inside** the MIDI RECORDER card and horizontally centred on the card, with a clear inset from the card border — not floating below it. |
| D2 | Resize to the smallest supported size (640 x 520) and the largest (1280 x 1040). | The pill stays inside the recorder card and centred at every size; it never overlaps the card boundary or the take rows. |
| D3 | Click **RECORD ARM**. | The pill relabels to **CANCEL**; the status line reads **ARMED - PLAY TO RECORD**. |
| D4 | Play a chord on the on-screen piano. | The status line switches to **RECORDING**. |
| D5 | Click **STOP TAKE**. | The pill returns to **RECORD ARM**; the status reads **1 TAKE STORED**; a take pill appears inside the card. |
| D6 | Click **RECORD ARM**, play, then click **CANCEL**. | The status returns to **WAITING FOR ARM** (or the stored-take count) and **no** new take appears. |
| D7 | Repeat until three takes are stored. | At most three take pills are ever listed, newest first. A fourth take evicts the oldest. |
| D8 | Drag a take pill into a MIDI track in Logic. | A `.mid` file is dropped; the take plays back as the recorded chord notes. |

## E. Branding

| # | Step | Expect |
| --- | --- | --- |
| E1 | CHORD page, bottom band. | The **real Music-Prod wordmark image** is visible at the reference position, at its native 260 x 86 aspect ratio. |
| E2 | Zoom in on it. | It is the actual asset (letterforms, spacing and weight of the Music-Prod logo), **not** a substitute text wordmark. |
| E3 | Resize the window. | The logo scales proportionally with the rest of the design, keeping its aspect ratio. |

## F. INFO page

| # | Step | Expect |
| --- | --- | --- |
| F1 | Open INFO. | `CHORD ENGINE`, `Version v0.1.0`, the ACCOUNT block, the UPDATES block, the TRIAL block and the HELP & FEEDBACK card are all populated with real state. |
| F2 | Read the ACCOUNT block before signing in. | `User  Not signed in`, `Music-Prod+  NOT ACTIVE`, `License  Not active - Music-Prod+ unlocks full access`. |
| F3 | Click **CHECK FOR UPDATES**. | The **Status** row changes to `Open Music-Prod Studio to check for updates`. The **Latest version** row keeps `-`. |
| F4 | Click **OPEN MUSIC-PROD STUDIO**. | Music-Prod Studio opens (or the OS reports it cannot be found). The plugin does not claim success either way. |
| F5 | Click **SIGN IN**. | The button relabels to **CANCEL LINK**, a browser page opens with a pairing code, and that code appears in the INFO hint line. |
| F6 | Complete the approval in the browser while signed in to Music-Prod. | Within a few seconds the INFO page shows `User  <your display name>`, `Music-Prod+  ACTIVE` (if your account is Music-Prod+) and `License  Music-Prod+ verified`; the button becomes **SIGN OUT**. |
| F7 | Click **CANCEL LINK** during a pending link instead of approving it. | The link stops, the hint reports that the link was cancelled, and the button returns to **SIGN IN**. |
| F8 | Click **SIGN OUT**. | The session returns to `User  Not signed in`; the button returns to **SIGN IN**. |
| F9 | Watch the TRIAL row. | It shows a live `Trial access  MM:SS remaining` countdown that decreases about once a second while the plugin is used. |
| F10 | Leave the plugin running until the trial reaches `00:00`. | The countdown reaches `00:00`, the row reads `Access expired`, and the ACCESS EXPIRED gate appears. |
| F11 | With the gate up, play a chord (on-screen or external). | **No chord is generated at all** — no MIDI out, no piano highlight. This is the reference expired-trial behaviour. |
| F12 | With the gate up, click **SIGN IN** on the gate and approve. | The gate dismisses and chord generation returns. |
| F13 | Click **OPEN HELP & FEEDBACK**. | The browser opens `https://music-prod.com/plugin/chordengine-feedback` **exactly**. |
| F14 | Restart the plugin (close and reopen the DAW session). | The trial starts a fresh 30:00 session (reference v0.5 fresh-session policy). A persisted signed-in session is restored and re-verified. |
| F15 | Search the whole UI for broken characters. | No mojibake, no empty labels, no `undefined`/`null`/`NaN` text anywhere. |

## G. Existing MIDI behaviour (regression)

| # | Step | Expect |
| --- | --- | --- |
| G1 | Route ChordEngine to a downstream instrument on a MIDI track. | Chord notes reach the instrument. |
| G2 | Play single trigger notes across the range. | The matching chord is generated; the trigger note itself is never forwarded. |
| G3 | Change **KEY**, **SCALE**, **CHORD PRESET**. | The chord context line and the generated chord follow immediately. |
| G4 | Press **-** twice and **+** twice. | Transpose steps 0 → -1 → -2 and stops; 0 → +1 → +2 and stops. Core state and MIDI both follow. |
| G5 | Toggle **WHOLE / LOWEST / HIGHEST**. | The active target is visibly selected and the voicing changes accordingly. |
| G6 | Change velocity mode and fixed velocity, then play. | Generated velocity matches section A. |
| G7 | Arm the recorder, play on the on-screen piano, stop, export. | Matches section D. |
| G8 | Use the INFO navigation (CHORD / INFO). | Both pages switch; the piano releases any held key when leaving the CHORD page. |

---

## Reporting

For each failing line, record:

* the exact step number,
* what was expected vs what happened,
* a screenshot,
* the host and the plugin format under test.

Do not work around a failure by editing the plugin, the host session or the
reference projects.
