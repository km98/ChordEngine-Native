# Native GUI reference inventory (source-static)

**Evidence class:** read-only source inspection of the editable original HISE UI and the approved ChordEngine FL 0.4.0 VST3 embedded script/resource, plus native JUCE implementation mapping. No reference file was modified. This is not a runtime visual-parity claim. The native GUI uses JUCE/C++ only. For embedded MIDI behavior, see [FL_REFERENCE_BEHAVIOR_CAPTURE.md](FL_REFERENCE_BEHAVIOR_CAPTURE.md); editable HISE findings are in [REFERENCE_FUNCTIONAL_INVENTORY.md](REFERENCE_FUNCTIONAL_INVENTORY.md). Separately, the native AU routing host observation is recorded in [HostObservations.json](../Tests/HostObservations.json); fixture-vector evidence remains source-static and `hostObserved=false`.

## Original HISE project GUI

- **Canvas/branding:** `Content.makeFrontInterface(800, 650)`; title text “CHORD ENGINE”, Music-Prod logo resource, chord name/notes area, piano panel, and footer. No `setResize` or comparable responsive scaling behavior was found in inspected source; nominal fixed layout only.
- **Selectors:** preset combo labels `Init`, `Soft`, `Wide`, `Deep`, `Bright`, `Dark`, `Punchy`; key and scale selector controls are not present in the inspected main script. The combo is not connected to an event handler applying a preset.
- **Piano:** C2–C6 nominal visual panel, MIDI 36–96, C labels; source paint uses distinct colors for trigger and generated notes. No event callbacks populate live display state in the saved original script.
- **Transpose:** no target selector or octave controls discovered.
- **Velocity:** combo choices Dynamic / Maximum / Fixed; fixed value slider 1–127, step 1, initialized to 100 and disabled at startup. Main source contains no `onControl` wiring for those visible elements.
- **Recorder/internal audio/info/help/licensing:** no recorder, INFO, trial/licensing display, or Help & Feedback control found in the inspected original main UI script. XML includes a Sine Wave Generator graph, but whether this is active customer-facing audio is not established from this UI script. No responsive resize evidence.
- **Defaults visible in source:** “No Chord”; Dynamic; fixed velocity 100 (slider disabled); preset combo initially at its first item. The actual selected preset relationship and live UI/runtime behavior are not proven by source alone.

## Approved embedded FL 0.4.0 GUI

The approved artifact has a distinct embedded script from the old editable HISE/ChordEngine-FL scripts. Findings below refer only to that embedded script and are **source-static**.

| Customer-facing element | Embedded FL evidence |
|---|---|
| Title/brand | 800×650 canvas, Music-Prod logo and two-tone ChordEngine wordmark; CHORD / INFO navigation |
| Key selector | 12 chromatic keys (C through B) |
| Scale selector | 8 scales: Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Harmonic Minor |
| Chord preset | 12 named presets: Basic, Pop, Piano, Emotional, Dreamy, Cinematic, R&B, Neo Soul, Lo-Fi, House, Deep, Ambient |
| Chord display | Chord well/name and note names |
| Piano/key display | C2–C7, MIDI 36–96; red trigger highlight, blue generated-note highlight |
| Transpose | WHOLE / LOWEST / HIGHEST target selector; separate octave offsets stored per target and preset; step range −2…+2 |
| Recorder | Record Arm / Cancel / Stop Take states; capture begins with first generated note; draggable MIDI takes, three take rows |
| Velocity | Dynamic / Maximum / Fixed. Dynamic follows input; Maximum outputs 127; Fixed uses slider value. Slider range 1–127, step 1, default 100; disabled unless Fixed selected |
| Internal Audio | Toggle exists and defaults OFF; enables the original internal sine-synth path in that product |
| INFO/licensing/trial | INFO area displays account/trial/license information and controls sign-in/out/update in the embedded product. Native licensing is explicitly outside this milestone. |
| Help & Feedback | Control exists; embedded source points to `https://music-prod.com/plugin/chordengine-feedback` (the read-only source script includes the URL). Native GUI opens that exact URL in the system browser; no account/licensing behavior is connected. |
| Other visible behavior | Page navigation and status/activation messaging; recorder take list and action state; feedback/account actions are tied to the embedded FL runtime. |
| Defaults/states | C / Minor / Dreamy / WHOLE at 0; Dynamic / fixed value 100; recorder and take state initialized empty; Internal Audio OFF. Fixed-velocity slider is disabled outside Fixed mode. |
| Resizing | Fixed 800×650 canvas found; no resize/scaling API call found in inspected embedded script. Runtime host behavior is not established. |

### Visual relationships/layout

- The embedded source declares a dark charcoal fixed 800×650 front canvas, Music-Prod logo and two-tone ChordEngine wordmark, with CHORD/INFO navigation separating the main performance view from product/account information.
- The CHORD view combines the chord name/note well with Key, Scale, and preset choices, then shows a broad piano range and transpose target/octave controls. Velocity, recorder/take controls, and the Internal Audio toggle are also visible in this view. Recorder takes use three rows; the action button changes state for arm/cancel/stop.
- INFO is a distinct page for activation/trial/account/update information and Help & Feedback, rather than another musical control group.
- The extracted source establishes the canvas size and grouping, but this inventory does not assert exact run-time pixel positions, resize behavior, or visual parity.

The approved FL source is a separate interface/runtime from the original HISE project. In particular, the embedded FL GUI's richer key/scale/transpose/recorder/info controls must not be attributed to the original HISE source. The embedded reference is also not a runtime visual capture.

### Reference geometry read from the embedded script (source values)

The parity pass reads these declarations directly from the read-only embedded script; they are the authoritative layout values the native editor reproduces.

| Element | Reference geometry (800×650) |
|---|---|
| Canvas / frame | `makeFrontInterface(800, 650)`; background panel inset 4 px, radius 10, border `0xFF2A3140`, two low-alpha accent glow rings |
| Header wordmark | `wordmarkPanel` (200, 8) 360×48; per-letter cells 22 px wide, 14 px word gap, `CHORD` `0xFFE8EBEF` / `ENGINE` `0xFF8A919C`, 2 px accent rule at y=43 |
| Navigation | `navChordButton` (528, 8) 112×26, `navInfoButton` (648, 8) 112×26; raised `0xFF1C2028` / track `0xFF232830`, text `0xFFE8EBEF`, font 10 bold |
| Chord well | (40, 78) 720×118, gradient `0xFF181D24` to `0xFF12161C`, radius 10 |
| Chord readout | chord name (56, 86) 320×36 font 26 bold, dim `0xFF5E6670` when idle and `0xFFF2F3F5` when a chord is displayed; notes (56, 126) 320×16 font 11 `0xFF8A919C` |
| Transpose cluster | `TRANSPOSE` (56, 154) 84×20 font 10 bold `0xFFAEB7C4`; `N OCT` chip (148, 144) 64×28 font 14 bold on `0xFF202834`; steppers (216, 144) and (248, 144) 28×28 pills with drawn bar glyphs; targets WHOLE (56, 176), LOWEST (140, 176), HIGHEST (224, 176) each 80×18, active fill `0xFF3E8BFF` and inactive `0xFF273040` |
| Harmonic selectors | KEY label (188, 200) + combo (188, 212) 92×26; SCALE (304, 200)/(304, 212) 92×26; CHORD PRESET (420, 200) + combo (420, 212) 192×26 |
| Piano | (40, 244) 720×216, bed `0xFF101318`, radius 10; margins 12 px sides / 8 px vertical; black keys 58 % width, 62 % height, offset 29 % of a white key; C-only labels font 8 bottom-left |
| Velocity | `velModeCard` (40, 478) 348×78 with caption (56, 490) and combo (56, 510) 168×34; `fixedCard` (408, 478) 352×78 with caption (432, 490), slider (432, 510) 240×34 and value (688, 510) 56×34 font 14 bold |
| Recorder | card (424, 80) 316×82 inside the chord well; `MIDI RECORDER` (438, 82) 110×13 font 10; state line (548, 82) 178×13 right-aligned font 9; three take pills (438, 98/119/140) 288×20, accent fill, two lines `TAKE n - N NOTES` / `DRAG MIDI`; action pill (536, 166) 92×28 (ARM `0xFF3E8BFF` / CANCEL `0xFF6A7280` / STOP TAKE `0xFFE05A5A`) |
| Internal audio | card (560, 556) 200×64, caption (574, 564) 130×14 font 10 bold, sub-caption `Generate internal sound` (574, 580) font 8 `0xFF5E6670`, OFF pill (676, 566) 56×32 radius 8 on `0xFF273040` with accent outline |
| Music-Prod branding | wordmark image slot (270, 560) 260×86 - the only Music-Prod branding element; serialized `brandLabel`/`brandingLabel` are explicitly hidden |
| Footer | (620, 626) 160×16 font 9 right-aligned `0xFF4A515C`, text `Chord Engine v` + `CE_PRODUCT_VERSION` (`0.4.0`) |
| INFO page | page panel (40, 46) 720×560 `0xFF10141A`; headline (64, 74) font 20 bold; version (64, 108); ACCOUNT header (64, 160) with user (64, 184), `Music-Prod+ NOT ACTIVE` (64, 208) amber `0xFFE0B050`, license (64, 232); UPDATES header (64, 280) with current (64, 304), latest (64, 328), status (64, 352) and buttons at y=384; TRIAL header (64, 448) with status (64, 472); Help & Feedback card (64, 508) 560×98 with `OPEN HELP & FEEDBACK` (80, 568) 220×28 |
| Shared chrome | background, wordmark, Music-Prod wordmark, footer and both nav buttons stay visible on either page |

### Reference paint/terminology detail used by the native editor

- Trigger note coral approx `0xFFE6524D`; generated notes blue `0xFF4F8FE0` (white keys) and `0xFF4073CC` (black keys); clean white key `0xFFE0E3E8` with `0xFFB3B8BF` border; clean black key `0xFF121217`; C-key label `0xFF6B7380` font 8.
- Idle chord readout shows the selected context as `<Key> <Scale> - <Preset>`; an active chord shows a chord symbol (`C`, `Cm`, `D#maj7`, ...) built from the resolved root plus the reference suffix table (`ceChordSuffix`), with the note list in brackets.
- Recorder state line wording: `WAITING FOR ARM`, `ARMED - PLAY TO RECORD`, `RECORDING`, `N TAKE(S) STORED`; action labels `RECORD ARM`, `CANCEL`, `STOP TAKE`.
- INFO signed-out copy: `User  Not signed in`, `Music-Prod+  NOT ACTIVE`, `License  Not verified`, `Current version`, `Latest version  -`, `Status  -`, `Trial access  -`, `Need help or want to report a problem with ChordEngine?`.
- The reference steppers paint bar glyphs (minus bar, plus cross) rather than font characters - the native editor adopts that so no minus/plus encoding can be corrupted.

### Native implementation notes (reference-parity pass)

- The editor paints the reference 800×650 design space and maps every child bound and card through one uniform scale plus centring offset, so the hierarchy and spacing follow the reference at any size. Resize limits 640×520 to 1280×1040 keep the reference 16:13 aspect.
- Reference font sizes are preserved in design units and scaled with the same factor (`applyTypography`), including the combo-box font supplied through a shared look and feel.
- Every user-facing string is ASCII-only and the reference glyphs that remain are drawn geometry (stepper bars) or plain text, so JUCE's `String(const char*)` ASCII decoding cannot produce mojibake.
- The Music-Prod wordmark is native text in the reference (270, 560) 260×86 slot; the reference logo image asset is not copied. The upper-left branding label was removed - the reference has no top-left Music-Prod label and hides its serialized brand labels.
- Chord readout: idle shows the reference context line with the Core's C4 preview chord in the note slot; while a trigger is held it shows the reference-style chord symbol for the resolved trigger plus the notes actually sounding (from the output-note snapshot). Both come from real Core state; the suffix table is a display-only mirror of the reference naming.
- Piano: reference bed, margins, key colours and C-only labels with coral trigger and blue generated highlighting; the C4 preview receives a subdued native-only tint (documented divergence).
- Recorder: state machine and export path unchanged; presentation uses the reference card, caption, right-aligned state line, three two-line `TAKE n - N NOTES` / `DRAG MIDI` pills and the state-coloured action pill. `N NOTES` comes from a new read-only processor accessor.
- Navigation: CHORD/INFO pills keep the reference geometry and colour language; the active page is marked with the track fill, bright text and a subdued accent outline (a native approximation of the reference active state).
- INFO page: reference structure and signed-out copy; `CHECK FOR UPDATES`, `OPEN MUSIC-PROD STUDIO` and `SIGN IN` keep their reference position and language and are now bound to their real actions (see the NOTES AT THE END of this document); Help & Feedback opens the exact reference URL.
- Internal audio: not implementable in this build. The plugin is a MIDI effect (`aumi`, `IS_MIDI_EFFECT TRUE`, `BusesProperties()` with no audio buses), while the reference controlled its internal `Sine Wave Generator1` processor bypass. Enabling real internal audio would require adding an audio output path, i.e. changing the AU/VST3 architecture, which is out of scope. The card shows a read-only, non-button `N/A` status chip (`MIDI effect - no audio bus`) with a tooltip, and the limitation is documented instead of faked.
- Piano: the reference v0.5 script's clickable/injecting piano IS implemented natively. The C2-C7 key bed is a real input surface: pressing a key pushes a raw note-on through a lock-free FIFO into `processBlock`, where it takes the identical Core path as an external MIDI note, and releasing sends the matching note-off. Painting and hit testing share one key geometry, so the key that lights up is the key that sounds. The editor contains no chord-resolution code of its own.
- Divergences kept on purpose: native-only preview tint; the honest `N/A` internal-audio chip (the reference offers an `ON`/`OFF` switch that this build cannot support); the real Music-Prod image wordmark (the earlier text substitute is gone); and the recorder card/pill geometry documented above.

## Native JUCE milestone mapping (earlier pass, layout superseded)

- **Native layout relationships (superseded by the reference-parity mapping above):** an early pass placed Music-Prod text and the ChordEngine wordmark in the header with the chord preview beside the selectors. That layout is replaced; Music-Prod now sits in the reference bottom branding slot and the readout occupies the chord well.
- **Native GUI display:** brand is represented with native text, not copied external logo artwork.
- **Native responsive policy:** default 800×650 with calculated proportional bounds and resize limits 640×520 through 1280×1040. This is a native implementation choice, not behavior copied from the fixed-size source reference.
- **Implemented and connected:** native C++ header/navigation, key and scale selectors, chord preset selector, responsive piano visualization, transpose target/octave, velocity mode/value, preview chord names/notes, and timer-polled trigger/generated note highlights.
- **Recorder:** native C++ state machine arms, captures generated chord events from the processor's MIDI-thread path, cancels/stops, retains a rolling three-take history, and provides drag-start exports of `.mid` files on the message thread. Audio-side capture uses a try-lock atomic flag and skips a capture event if a state transition owns the storage; UI stop/reset wait only outside processBlock. It does not record raw trigger notes or touch UI components from processBlock. GUI file-drop behavior requires manual Logic verification.
- **INFO/account/updates/trial (superseded by the functional passes):** the INFO page is no longer placeholder state. The account rows, the update check (which honestly reports there is no plugin-facing update API rather than inventing a version), the Music-Prod device-code sign-in, sign-out, and a real 30:00 trial clock with the reference `ACCESS EXPIRED` gate are all implemented natively. Internal Audio remains a read-only `N/A` status chip because a MIDI-effect build has no audio buses; see the internal-audio note above.
- **State/processing:** explicit validated processor configuration accessors publish packed lock-free atomic control state; the audio/MIDI thread applies configuration at a process-block boundary. The editor never touches MIDI buffers. The GUI does not claim exact HISE/FL visual parity.

---

## Functional-pass corrections (2026-10-05, source-static)

Read-only re-inspection of the authoritative reference
(`ChordEngine-FL/Build/layout-info-card-authoritative-pristine-20261002.js`)
settled four items that the earlier pass had left approximate.

### 1. Recorder action pill

The reference documents its recorder geometry explicitly (lines 1466-1482):

```
recCard         424,  80  316 x  82   inner card surface (STAGE 5I:
                                    compacted so the recording control
                                    can sit under it - pills tucked up)
recActionButton 536, 166  92 x 28   (centred under the recorder card:
                                    536 + 46 = 582 = the card centre,
                                    4 px below the card bottom at y=162,
                                    bottom edge 194 inside the well at 196)
```

So the reference deliberately places the pill **4 px below** its own 82 px
card surface, relying on the surrounding chord well to contain it. That is
what the product owner reported as "too far down / outside the recorder area",
so the native editor deviates deliberately and minimally:

* the card surface extends to **height 114** (bottom edge 194, still 2 px
  inside the well's 196), and
* the pill moves up to **y = 163** (bottom edge 191) and stays at the card's
  centre axis: x = 536, 536 + 46 = 582 = the card centre (424 + 158).

That leaves the pill with exactly **3 px of clearance above** (the last take
row ends at 160) and **3 px below** (inside the card's bottom edge at 194):
the pill is fully inside its own card, never touching its boundary, and
horizontally centred, while every other child bound keeps its reference value.
The deviation from the reference pill's y = 166 is 3 px. Card rectangle and
pill placement are named class constants shared by `paint()`, `resized()` and
the layout test, which asserts containment, centring (within 1 px) and a
visible inset at 800x650, 640x520 and 1280x1040.

### 2. Velocity

Reference `onControl(velocityModeCombo)` disables the fixed-velocity slider in
Dynamic and Maximum and enables it only in Fixed; `onControl(fixedVelocitySlider)`
writes the live numeric readout. The native editor already matched this; the
functional pass added the automated proof (slider values 1 / 64 / 100 / 127
reach Core state and the generated MIDI velocity).

The product owner reported the control as reading like a static graphic. The
root cause is that a disabled JUCE slider keeps its full colouring, so the
`Dynamic` / `Maximum` state looked live while ignoring the mouse. The control
is a real `juce::Slider` and always was interactive in `Fixed` mode; the fix
publishes the disabled state honestly as well (the whole interactive half of
the card drops to ~35% opacity outside `Fixed`, and the tooltip states why),
and the automated test now drives it through real `mouseDown` / `mouseDrag` /
`mouseUp` events rather than a `setValue` shortcut.

### 3. Internal Audio — architectural limitation

Reference implementation (lines 895-940, 949-1030):

- the sound engine is the original `Sine Wave Generator1` child synth,
  obtained with `Synth.getChildSynth("Sine Wave Generator1")`;
- the switch calls `ceAudioSynth.setBypassed(!ceInternalAudioOn)`;
- `CE_INTERNAL_AUDIO_DEFAULT_ON = false`, and the state is **not persisted**;
- the trial lockout (`ceSetAudioMuted` -> master `GainModulation` intensity)
  sits above that synth.

`setBypassed()` requires an **audio output bus**. The native product is an
AU `aumi` MIDI processor and a VST3 MIDI effect: `isBusesLayoutSupported()`
accepts layouts only when both the input and the output bus lists are empty.
There is therefore no audio path to bypass, and adding one would change the
plugin format and the host routing the product is built on.

Decision (confirmed with the product owner for the fix pass): **no
architecture change; the product stays a pure MIDI processor in both
formats.** The card keeps the reference geometry (560, 556, 200 x 64) and the
reference caption `INTERNAL AUDIO`, but the chip is a plain (non-`Button`)
status surface in the disabled palette reading `N/A` — *not* `ON`/`OFF`, so it
cannot imply a state that could be switched — the sub-caption states
`MIDI effect - no audio bus`, and the tooltip explains the limitation. It has
no click handler at all, never a fake ON/OFF toggle, and nothing in the MIDI
path depends on it.

Why the VST3 cannot provide it either: making the VST3 audio-capable would
require an audio output bus, which would stop it being a MIDI effect, change
its plugin identity, and change how hosts route it; the AU `aumi` MIDI FX
could not have one at all in that shared design. An automated test pins the
architecture instead of the promise: `isMidiEffect()` is true, the empty
(MIDI-only) layout is supported, and a stereo output bus is explicitly
rejected. If internal audio is ever wanted, the correct shape is a **separate,
additionally named audio-capable instrument target**, not a changed MIDI
effect.

### 4. Music-Prod logo asset

The reference renders a real image, not text:

```
const var logoPanel = Content.addPanel("logoPanel", 360, 560);   // -> 270, 560
logoPanel.loadImage("{PROJECT_FOLDER}MusicProdLogo.png", "musicProdLogo");
g.drawImage("musicProdLogo", [0, 0, 260, 86], 0, 0);
```

The asset is `ChordEngine/Images/MusicProdLogo.png` /
`ChordEngine-FL/Images/MusicProdLogo.png`, 260 x 86 RGBA PNG, MD5
`9f221d8f5555684d91474bba301a43d9` (both copies byte-identical). It is copied
verbatim to `Resources/MusicProdLogo.png`, embedded with
`juce_add_binary_data(ChordEngineAssets ...)` and drawn 1:1 into the reference
slot `(270, 560, 260, 86)`. The earlier cell-spaced text stand-in is gone.
Paint order matches the reference: the logo is drawn before the INFO card, so
the card covers it on the INFO page exactly as `logoPanel` is covered by
`infoPagePanel` there.

### 5. INFO page, trial, updates and account

All of the following are ported from the reference and implemented for real
(see [SECOND_MAC_FUNCTIONAL_GUI_TEST.md](SECOND_MAC_FUNCTIONAL_GUI_TEST.md)
for the manual checklist and `Source/Licensing/` for the code):

- `CHECK FOR UPDATES` -> `ceCheckForUpdates()`: there is no plugin-facing
  update API, so the status row reports `Open Music-Prod Studio to check for
  updates` and the latest-version row keeps `-`. No version is fabricated.
- `OPEN MUSIC-PROD STUDIO` -> `Engine.openWebsite("musicprodstudio://open")`,
  the app's registered URL scheme. No bundle id or path is guessed.
- `SIGN IN` -> the existing Music-Prod device-code flow against
  `functions/v1/vyre-plugin-auth` (`start` / `poll` / `entitlements` /
  `logout`), with no API key or secret and no user id or email sent.
- `SIGN OUT` shares the SIGN IN slot; exactly one of the pair is ever visible.
- Trial: 30:00 of use (`CE_TRIAL_TOTAL_MS = 1800000`), started by the first
  generated chord, decrement-only, not consumed by a licensed session, with
  the reference v0.5 fresh-session policy.
- Expiry: the ACCESS EXPIRED gate (backdrop, 440 x 210 card at 180, 220,
  `OPEN MUSIC-PROD STUDIO` + `SIGN IN`), and an expired unlicensed session
  generates no chord at all.
