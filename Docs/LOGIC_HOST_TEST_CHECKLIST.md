# Logic Pro Manual Host-Test Checklist (ChordEngine MIDI FX 0.1.0)

Status: **NOT HOST-VERIFIED.** Nothing in this file has been run in Logic Pro.
Every step is manual. Do not automate Logic and do not script the DAW.

## Build under test

| Format | Installed path | Identity |
| --- | --- | --- |
| AU | `~/Library/Audio/Plug-Ins/Components/ChordEngine.component` | `aumi` / `Abcd` / `Abcd`, bundle `com.myCompany.product` |
| VST3 | `~/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3` | bundle `com.musicprod.chordenginefl`, `Musc` / `Cefl` |

Both are universal `x86_64 arm64`, version `0.1.0`, minimum macOS 12.0.
Before testing, confirm the installed binary hashes match the current Release build.

## Preparation

1. Use a **new, disposable** Logic project. Do not open or save an existing session.
2. Insert ChordEngine as a **MIDI FX** on a software-instrument track, with an instrument downstream.
3. Record the Logic version and macOS version in the results.

## A. Insertion and discovery

- [ ] A1. ChordEngine appears in the MIDI FX slot list (AU, Music-Prod).
- [ ] A2. The plugin window opens without a crash and shows the CHORD view.
- [ ] A3. Closing and reopening the window works repeatedly.

## B. Musical controls

- [ ] B1. Key changes the generated chord pitch set.
- [ ] B2. Scale changes all 8 scales (major, minor, dorian, phrygian, lydian, mixolydian, locrian, harmonic minor).
- [ ] B3. Chord preset changes all 12 presets (basic, pop, piano, emotional, dreamy, cinematic, R&B, neo soul, lo-fi, house, deep, ambient).
- [ ] B4. Transpose target WHOLE, LOWEST and HIGHEST each change which notes move.
- [ ] B5. Transpose octave runs -2 to +2 steps, and the readout matches the state.
- [ ] B6. Velocity mode Dynamic, Maximum 127, Fixed. Fixed accepts 1 to 127.
- [ ] B7. The fixed velocity value appears as the note-on velocity in Logic's MIDI editor.

## C. Piano and MIDI behaviour

- [ ] C1. An on-screen piano press produces the chord through the MIDI FX output.
- [ ] C2. Dragging between piano keys releases the old chord and triggers the new one.
- [ ] C3. Releasing the key sends note-off for every generated note.
- [ ] C4. External MIDI keyboard input generates the same chords.
- [ ] C5. Sustain pedal (CC64) holds generated notes until released.
- [ ] C6. Panic (CC120 and CC123) silences all generated notes.
- [ ] C7. Unrelated MIDI (for example CC1 or pitch bend) passes through unchanged.
- [ ] C8. The plugin produces no audio.

## D. Recorder

- [ ] D1. Arm the recorder, play, then stop. A take appears.
- [ ] D2. Cancel discards an armed or recording take.
- [ ] D3. Take rows show a note count that matches what was played.
- [ ] D4. Drag a take into a MIDI track. A `.mid` file is created and plays back the chord notes.

## E. State persistence

- [ ] E1. Set non-default key, scale, preset, transpose target and octave, velocity mode and fixed velocity.
- [ ] E2. Save the Logic project, close it, and reopen it.
- [ ] E3. Every control in E1 is restored to its saved value in the UI.
- [ ] E4. Generated chords after reopening match the values before save.
- [ ] E5. A project with no ChordEngine state opens with the defaults (key C, minor, default preset, transpose 0, Dynamic).
- [ ] E6. Two ChordEngine instances in the same project keep independent settings.
- [ ] E7. Sustain, panic, recorder takes and held notes are **not** restored after reopening. This is intended.

## F. Licensing, update and INFO

- [ ] F1. The INFO page shows `CHORD ENGINE`, the version, and the ACCOUNT, UPDATES, TRIAL and HELP & FEEDBACK sections.
- [ ] F2. Update status is shown honestly. The live endpoint currently reports "No published release".
- [ ] F3. The trial counts down only after the first real chord is generated.
- [ ] F4. Sign-in (device-code flow) completes end to end, if the licensing backend allowlists ChordEngine. This is unverified; see the release-gate report.
- [ ] F5. Offline behaviour shows a clear message and does not crash.

## G. Stability

- [ ] G1. Play for at least 10 minutes with the transport running. No dropouts and no hung notes.
- [ ] G2. Quit Logic with the plugin open. No crash.
- [ ] G3. Rescan plugins. ChordEngine is listed once, not duplicated.

## Results

Record each item as Pass, Fail, or Not tested, with the Logic and macOS versions. A failure is a release blocker until it is fixed or explicitly accepted.

| Date | Tester | Logic version | macOS | Result summary |
| --- | --- | --- | --- | --- |
|  |  |  |  |  |
