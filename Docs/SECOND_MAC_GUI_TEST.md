# Second-Mac Logic GUI test — development AU

**Manual test only; no DAW automation.** Use a **new disposable Logic project only**. Do not open or save an existing Logic project. The critical AU MIDI route was already manually verified by Martin on a clean second Mac; the GUI-specific checks below remain a separate test.

## Safe setup

- Copy [ChordEngine-Native-macOS-AU-GUI-Test.zip](../Build/TestArtifacts/ChordEngine-Native-macOS-AU-GUI-Test.zip) to a clean second Mac and verify its SHA-256 against [MANIFEST.txt](../Build/TestArtifacts/MANIFEST.txt).
- Install the test AU manually on that second Mac only. Do not install it on the development Mac.
- Create a new disposable Logic project. Keep monitoring volume low. The plug-in is MIDI-only; any sound should come from a downstream software instrument.
- Do not modify the HISE project, ChordEngine-FL, installed reference plugins, SideChainer, or release artifacts.

## Checklist

1. In Logic's MIDI FX browser, confirm **Music-Prod → ChordEngine** appears.
2. Insert it as MIDI FX and confirm the complete GUI is visible at 800×650. Resize within the allowed 640×520 to 1280×1040 range; verify sections remain usable.
3. Change **Key** and confirm the selection and generated chord update.
4. Change **Scale** (for example Minor to Major) and verify chord notes respond.
5. Change **Chord Preset** and verify the selected preset changes output.
6. Inspect the piano display at C2–C7. Verify preview notes are displayed and trigger/generated notes light on note-on and clear on note-off.
7. Try transpose targets **WHOLE**, **LOWEST**, and **HIGHEST**; test octave steps −2 through +2 and both clamp boundaries.
8. Confirm a new instance shows **Dynamic / 100**. Try Dynamic, Maximum (127), and Fixed at values 1, 100, and 127.
9. Arm the recorder, play a generated chord, stop it, and inspect the stored-take rows. Cancel an armed empty capture. Drag a completed take into the disposable Logic project and confirm the MIDI file imports.
10. Open **INFO**. Confirm account/trial/licensing copy is explicitly placeholder/unknown; no authentication or activation behavior should be implied.
11. Click **HELP & FEEDBACK** and confirm it opens `https://music-prod.com/plugin/chordengine-feedback` in the browser.
12. Confirm valid trigger notes still generate the expected chord. For C4 / C Minor / Dreamy / WHOLE 0, compare with notes 51, 55, 58, 62 as a reference only.
13. Confirm generated MIDI reaches the downstream software instrument. This route was previously observed manually; repeat to regress after GUI changes.
14. Bypass or mute the downstream instrument and verify ChordEngine itself produces no audio.
15. Confirm only the new disposable project was used; do not open or save any existing Logic project, and do not alter the HISE implementation.

Record Logic/macOS versions, AU archive SHA-256, GUI dimensions, selected settings, observed chord/velocity, recorder drop result, Help launch result, downstream routing, and audio-silence result. Report each as pass/fail/blocked. The existing manual Logic routing observation is recorded as `hostObserved: true` in [HostObservations.json](../Tests/HostObservations.json); the 38 source-derived fixture cases remain `hostObserved: false` in [FLReferenceVectors.json](../Tests/FLReferenceVectors.json). Do not treat prior routing observation as GUI verification.

## Current limitations

- The native editor is a reference-informed JUCE/C++ implementation, not a claim of exact HISE/embedded-FL visual parity. No DAW GUI verification has been performed on this final build yet.
- Trial, licensing, and account UI are placeholders only; no licensing backend/authentication exists. Internal Audio is a disabled OFF control in this MIDI-only build.
- Plugin settings and recorder takes are not yet serialized into host project state. Recorder stores up to three generated-MIDI takes in memory and exports temporary `.mid` files during a drag operation.
- VST3 needs testing in an appropriate VST3 host separately; Logic GUI checklist covers AU.
