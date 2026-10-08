# Second-Mac Logic GUI parity test — development AU (reference layout pass)

**Manual test only; no DAW automation.** Use a **new disposable Logic project only**. Do not open, alter, or save any existing Logic project. Do not open HISE, ChordEngine-FL, SideChainer, or any installed release plugin during this test.

This checklist covers the **reference-parity GUI pass** (layout, typography, terminology, encoding, internal-audio status). It supersedes the GUI checks in [SECOND_MAC_GUI_TEST.md](SECOND_MAC_GUI_TEST.md) for visual verdicts; the earlier document remains the record of the previous milestone checklist. The AU MIDI route itself was already manually verified by Martin on a clean second Mac (see [HostObservations.json](../Tests/HostObservations.json), `hostObserved: true`); it is repeated here as a regression check only.

## Test build under test

| Item | Value |
|---|---|
| AU archive | `Build/TestArtifacts/ChordEngine-Native-macOS-AU-GUI-Parity-Test.zip` |
| AU ZIP SHA-256 | `7d366b49ddfa1b4d727e1e352250a81aa7845da753c6911eaca0834cb550e8ba` |
| AU executable SHA-256 | `5a873ebba063c9acf0222031d2fcb0a3c951ac6857f85fa63adb8ced4af553d6` |
| Architecture | universal `x86_64` + `arm64` |
| Plugin type | AU MIDI Processor (`aumi`), Music-Prod → ChordEngine |
| VST3 archive (optional secondary host) | `Build/TestArtifacts/ChordEngine-Native-macOS-VST3-GUI-Parity-Test.zip` |
| VST3 ZIP SHA-256 | `0fe46180be6b2fb741af0ddc05b14bae8c35e8c5a0670e92bc99389f63058d29` |

Verify the ZIP SHA-256 against [MANIFEST.txt](../Build/TestArtifacts/MANIFEST.txt) before installing. Install the test AU **manually on the clean second Mac only**; never install it on the development Mac. Unzip into `~/Library/Audio/Plug-Ins/Components/` and confirm `unzip -t` passed on the archive.

## Section A — visual checks

Compare against the reference geometry: fixed 800×650 canvas, header wordmark at (200,8) 360×48, CHORD/INFO navigation at (528,8) and (648,8) 112×26, chord well (40,78) 720×118, recorder card (424,80) 316×82 inside the well, Key/Scale/Preset row y=200..238, piano (40,244) 720×216, velocity cards (40,478) and (408,478) each 348×78/352×78, internal-audio card (560,556) 200×64, Music-Prod wordmark slot (270,560) 260×86, footer at (620,626) right-aligned. See [NATIVE_GUI_REFERENCE_INVENTORY.md](NATIVE_GUI_REFERENCE_INVENTORY.md) for the evidence map.

1. **Overall layout** matches that hierarchy: header/navigation on top, chord well with readout on the left and MIDI recorder on the right, harmonic selectors directly above a broad piano, velocity row below it, bottom band with Music-Prod branding, internal audio, and the version footer.
2. **No malformed characters anywhere.** Every caption, readout, tooltip, and combo item renders correctly: no `Â`, no `â`, no stray `·`/`–`/`…` substitutions, no box glyphs. Check the chord readout, piano C labels (`C2` … `C7`), footer (`Chord Engine v0.4.0`), INFO copy, and the recorder rows.
3. **Music-Prod branding placement** is the bottom band wordmark at the reference logo slot — **not** an upper-left corner label. The header carries the **ChordEngine** wordmark only (`CHORD` bright / `ENGINE` dim with a blue rule beneath).
4. **ChordEngine title / navigation**: `CHORD` and `INFO` buttons at the top right; the active page is visibly marked; both stay visible on either page.
5. **Chord well** shows the selected harmonic context while idle (`<Key> <Scale> - <Preset>`, e.g. `C Minor - Dreamy`) in dim text, with the Core's C4 preview chord in the note line, e.g. `[D#3 G3 A#3 D4]`.
6. **Piano** spans C2–C7 with labelled C keys, has a dark rounded bed, and highlights a held trigger note in coral and generated chord notes in blue.
7. **Transpose controls**: `TRANSPOSE` caption, `N OCT` chip, and the `-`/`+` stepper pills (drawn bar glyphs — the minus must be a clean horizontal bar and the plus a clean cross, never a corrupted character), plus the `WHOLE` / `LOWEST` / `HIGHEST` row where the active target is filled with the blue accent.
8. **Velocity section**: `VELOCITY MODE` card with the Dynamic/Maximum/Fixed selector, and the `FIXED VELOCITY` card with a slider and a numeric value; the slider reads as inactive unless Fixed is selected.
9. **Recorder panel**: `MIDI RECORDER` caption, right-aligned state line (`WAITING FOR ARM`, `ARMED - PLAY TO RECORD`, `RECORDING`, `N TAKE(S) STORED`), and the blue action pill whose label changes between `RECORD ARM`, `CANCEL`, and `STOP TAKE`.
10. **INFO page**: `CHORD ENGINE` headline, `Version v0.1.0`, `ACCOUNT` (`User  Not signed in`, `Music-Prod+  NOT ACTIVE`, `License  Not verified`), `UPDATES` (current/latest/status rows and greyed `CHECK FOR UPDATES`, `OPEN MUSIC-PROD STUDIO`, `SIGN IN`), `TRIAL` (`Trial access  -`), and the `HELP & FEEDBACK` card. No development-only wording, no placeholder diagnostics text, no malformed characters.
11. **Internal audio card** shows `INTERNAL AUDIO` with the `Generate internal sound` caption and an `OFF` pill rendered in the reference OFF style. It is a **status surface**, not an interactive switch: clicking must do nothing, and there must be no internal audio in any state.
12. **Footer** reads `Chord Engine v0.1.0`, discreet bottom-right, and stays visible on both pages.

## Section B — functional checks

13. Logic's MIDI FX browser shows **Music-Prod → ChordEngine**.
14. **Key** selector changes the readout context and the generated chord.
15. **Scale** selector (e.g. Minor → Major) changes the generated chord.
16. **Chord Preset** selector changes the generated chord voicing.
17. **Transpose target** switches between WHOLE / LOWEST / HIGHEST without changing stored values except through the steppers; the readout chip updates.
18. **Transpose octave**: `0 → -1 → -2` stops at `-2`; `0 → +1 → +2` stops at `+2`; the chip shows `+1 OCT`, `0 OCT`, `-1 OCT`; generated chords follow the shift.
19. **Velocity**: new instance shows **Dynamic / 100**; Maximum outputs 127; Fixed uses the slider value and the slider is only editable in Fixed mode; the numeric readout follows the slider.
20. **Recorder**: `RECORD ARM` → play a chord → `STOP TAKE` stores a row; the pill shows `TAKE n - N NOTES` with `DRAG MIDI`; drag the pill into the disposable project and confirm the `.mid` imports with note timing; `CANCEL` on an armed, empty capture stores nothing. Internal audio stays silent throughout.
21. **Help & Feedback**: `OPEN HELP & FEEDBACK` opens `https://music-prod.com/plugin/chordengine-feedback` in the system browser. The URL must not change.
22. **Chord generation regression**: a valid trigger still produces the expected chord; for C4 / C Minor / Dreamy / WHOLE 0 the live readout should show the reference-style symbol and the generated notes on the piano.
23. **Downstream routing**: generated MIDI still reaches the downstream software instrument.
24. **No unexpected audio**: with the downstream instrument bypassed or muted, ChordEngine itself produces no sound.

## Section C — resize checks

25. **Normal size** (800×650): all sections visible and aligned as described.
26. **Larger size** (up to 1280×1040): layout scales uniformly; no clipped captions, no overlapping cards, no text overflow.
27. **Smaller supported size** (640×520): all controls remain usable; captions stay legible; nothing overlaps; the recorder rows and the chord readout remain readable.

## Record and report

Record Logic/macOS versions, AU ZIP SHA-256, chosen sizes, each selection, observed chord/readout, piano highlight behaviour, recorder export result, Help launch result, downstream routing, and the audio-silence result. Report every item as pass / fail / blocked. GUI results must be recorded separately from the already-confirmed AU MIDI routing observation; do not relabel the source-derived fixture vectors.

## Known limitations carried into this test

- **Internal audio cannot be implemented in this build.** The plugin is a MIDI effect (`aumi`, `IS_MIDI_EFFECT TRUE`, no audio buses). The reference controlled its own `Sine Wave Generator1` processor bypass; producing that audio natively would require adding an audio output bus, which changes the AU/VST3 architecture. Out of scope for this milestone: the OFF pill is a read-only status surface and the limitation is documented instead of faked.
- **Licensing/account/update/trial services are not connected.** The INFO page keeps the reference structure and copy for the signed-out state; `CHECK FOR UPDATES`, `OPEN MUSIC-PROD STUDIO`, and `SIGN IN` are present but disabled, and the trial row shows `-` rather than an invented countdown.
- **The Music-Prod wordmark is native text** in the reference logo slot; the approved product's logo image asset was not copied into the native build.
- **The preview tint** on the piano (subdued keys for the C4 preview chord) is a native-only addition; the reference highlights trigger and generated notes only.
- **Clickable piano / note injection** from the reference v0.5 script is not implemented; the piano is a live display of real Core/processor state.
- Host project persistence of plugin settings and recorder takes is still deferred; takes live in memory and export to temporary `.mid` files during a drag.
- VST3 needs a separate VST3 host check; this checklist covers the AU.

## Protected artifacts (read-only; must remain unchanged)

- Approved FL VST3 executable SHA-256 `3ffbe1f349846df64dc7e2cf3357b1e682e4f8f1dd3cf8a879080edc773483ed`.
- Protected ChordEngine-FL `Binaries/Source/Plugin.cpp` MD5 `1044c2719acb8d49573c5aadd9e7ca22`.
- No file outside `/Users/martin/Documents/ChordEngine-Native` was modified by this milestone.
