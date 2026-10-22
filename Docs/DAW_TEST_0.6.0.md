# ChordEngine 0.6.0: real-DAW test on the MacBook

The first install of the JUCE-free build (`Docs/NATIVE_REBUILD.md`) in real
hosts. Installer: `ChordEngine-0.6.0-macOS.dmg`, SHA-256
`2bae51dd5709e1c122045197c690fd497b1a41945365246b0a4b2b188438953a`
(build record: `Docs/RELEASE_0.6.0_CANDIDATE.md`). About 45 minutes.

Write down the result of every step marked **[record]**; failures are as
useful as passes.

## 0. Before installing 0.6.0: make the 0.5.1 projects

These projects prove that existing work opens. They must be saved **with
0.5.1**, before 0.6.0 is installed.

1. Check what is installed (Terminal):

   ```sh
   for b in /Library/Audio/Plug-Ins/Components/ChordEngine*.component \
            /Library/Audio/Plug-Ins/VST3/ChordEngine.vst3 \
            ~/Library/Audio/Plug-Ins/Components/ChordEngine*.component \
            ~/Library/Audio/Plug-Ins/VST3/ChordEngine*.vst3; do
     [ -e "$b" ] && echo "$b  $(defaults read "$b/Contents/Info.plist" CFBundleShortVersionString)  $(defaults read "$b/Contents/Info.plist" CFBundleIdentifier)"
   done
   ```

   **[record]** the output. Expected: 0.5.1 in `/Library/...` only. If no
   0.5.1 is installed, install the signed 0.5.1 DMG first (SHA-256 in
   `Docs/RELEASE_0.5.1.md`).

2. If anything is listed under `~/Library/Audio/Plug-Ins`: quit every DAW and
   move those bundles into a backup folder (e.g. `~/Desktop/ChordEngine-user-backup/`).
   They have the same AU codes and would shadow the system copies.

3. **Logic Pro, project "CE-051-MidiFx"**: a Software Instrument track (e.g.
   Electric Piano), MIDI FX slot → Audio Units → Music-Prod → **ChordEngine**.
   Set, and take a screenshot of the editor:
   Key **D#**, Scale **Dorian**, preset **Neo Soul**, TRANSPOSE **LOWEST −1**,
   VELOCITY MODE **FIXED**, FIXED VELOCITY **77**, recorder quantize
   **1/3 beat**, FOLLOW DAW RECORD **on**. Record one take in the recorder.
   Save the project.

4. **Logic Pro, project "CE-051-Instrument"**: an instrument track with
   **ChordEngine Instrument**; INTERNAL AUDIO on, sound **Bass**, Key **D#**,
   preset **Neo Soul**. Play a note to hear it. Save.

5. **FL Studio, project "CE-051-FL"**: add the **ChordEngine** VST3, route its
   MIDI output to an instrument the way you normally do, set the same values
   as in step 3, play a few notes, save.

6. Quit Logic and FL Studio.

## 1. Install 0.6.0

1. Open the DMG. **[record]** macOS opens it without a warning.
2. Double-click `ChordEngine-0.6.0-macOS.pkg`, install. **[record]** no
   Gatekeeper/notarization warning.
3. Run the command from step 0.1 again. Expected, **[record]**:

   ```
   /Library/Audio/Plug-Ins/Components/ChordEngine Instrument.component  0.6.0  com.musicprod.chordengine.instrument.au
   /Library/Audio/Plug-Ins/Components/ChordEngine.component  0.6.0  com.musicprod.chordengine.au
   /Library/Audio/Plug-Ins/VST3/ChordEngine.vst3  0.6.0  com.musicprod.chordengine.vst3
   ```

   and nothing under `~/Library`.

## 2. auval

```sh
killall -9 AudioComponentRegistrar 2>/dev/null
auval -a | grep -i musc
auval -v aumi Cefl Musc
auval -v aumu Cein Musc
```

Expected: `auval -a` lists `aumi Cefl Musc - Music-Prod: ChordEngine` and
`aumu Cein Musc - Music-Prod: ChordEngine Instrument`; each validation ends
with **`AU VALIDATION SUCCEEDED.`** **[record]** the last 30 lines of each run
(or the whole output if it fails).

## 3. Logic Pro

1. Start Logic. If ChordEngine is missing: Settings → Plug-in Manager → select
   the two Music-Prod entries → **Reset & Rescan Selection**. **[record]**
   whether a rescan was needed and that both are listed as "successfully
   validated".
2. Open **CE-051-MidiFx**. **[record]**: ChordEngine loads without a "plug-in
   not found" message and every value from step 0.3 is unchanged (compare the
   screenshot), including the recorded take.
3. Play notes: four-note chords reach the instrument; the chord name and the
   notes show in the editor. Sustain pedal (CC64) holds chords.
4. Editor: drag the bottom-right corner between the smallest (640×520) and
   largest (1280×1040) size; click piano keys (they sound); preset ◀ ▶; change
   Key, Scale, TRANSPOSE, VELOCITY; CHORD ↔ INFO. **[record]** anything that
   looks different from 0.5.1 or does not respond.
5. Recorder: arm, play, stop; drag a take from the editor onto a track:
   a MIDI region appears with the played chords. With FOLLOW DAW RECORD on,
   Logic's record button arms and stops the recorder.
6. Bypass ChordEngine in the slot and un-bypass it: notes pass through
   unchanged while bypassed.
7. Save the project as **CE-060-MidiFx**, close, reopen: same state.
8. Open **CE-051-Instrument**: internal Bass sound plays, values unchanged.
   Switch the track output to mono if possible: still sounds.
9. New project, new instrument track, MIDI FX → ChordEngine from scratch:
   defaults (preset **Dreamy**) and chords work.

## 4. FL Studio

1. Options → Manage plugins → **Find installed plugins** (rescan). **[record]**
   ChordEngine appears once (VST3, Music-Prod).
2. Open **CE-051-FL**: ChordEngine loads with every value from step 0.5, its
   MIDI output still drives the instrument. **[record]**
3. Repeat 3.3–3.5 inside FL (chords, editor, recorder drag to the playlist /
   piano roll). Save as **CE-060-FL**, reopen.

## 5. Account, licence and trial

Licensing data lives in `~/Library/Application Support/Music-Prod/ChordEngine/`
and is shared with 0.5.1, so the state should carry over.

1. INFO page: **[record]** what it shows (signed in / trial minutes left /
   licensed / demo) and whether it matches what 0.5.1 showed.
2. If signed out: SIGN IN → the browser opens music-prod.com, approve, the
   editor shows the account within a few seconds.
3. If you have a licence key to test: enter it under the licence key section,
   ACTIVATE → licensed.
4. Optional, needs a signed-out account without licence: the trial counts
   down only while ChordEngine processes audio; after 30 minutes it switches
   to demo.

## 6. Music-Prod Studio

Open Music-Prod Studio. Until Studio learns the new bundle ids (PR #14) it
may not recognise 0.6.0 as installed or may offer 0.5.1 as an update.
**[record]** what it shows; do **not** let it reinstall 0.5.1 during the test.

## 7. Report back

Send the **[record]** notes, the auval outputs, and screenshots of anything
unexpected. If something fails, also send `~/Library/Logs/DiagnosticReports/`
entries from the test time mentioning Logic, FL or AudioComponentRegistrar.

## Roll back to 0.5.1 (only if needed)

Quit all DAWs, then:

```sh
sudo rm -rf "/Library/Audio/Plug-Ins/Components/ChordEngine.component" \
            "/Library/Audio/Plug-Ins/Components/ChordEngine Instrument.component" \
            "/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3"
killall -9 AudioComponentRegistrar 2>/dev/null
```

and install the 0.5.1 DMG again. Projects saved with 0.6.0 open in 0.5.1
(the state format is unchanged).
