# The AppKit editor (JUCE-free build)

The 0.5.0 redesign (`Docs/UI_REDESIGN_0.5.0.md`) rebuilt on plain AppKit. The
0.5.1 editor (JUCE) was the reference: its layout numbers, palette, type
scale, drawing rules, wording and behaviour were ported one to one.

## Files

| File | Contents |
|---|---|
| `Source/UI/EditorHost.h` | The adapter interface (`EditorHost`), sizes, `createEditorView` / detach, and the programmatic access used by tests and the snapshot tool (`createEditorViewWithOptions`, `clickEditorControl`, `selectEditorItem`, ...) |
| `Source/UI/EditorStyle.h/.mm` | Palette, faces (bundled Inter / Schibsted Grotesk loaded process-privately with `CTFontManagerCreateFontDescriptorsFromURL`; system Baskerville and Snell Roundhand Bold as in 0.5), text drawing with 0.5's metric rules, shapes, the re-tinted Music-Prod mark |
| `Source/UI/EditorWidgets.h/.mm` | One AppKit view class (`CEWidgetView`) forwarding drawing and mouse events to small C++ widgets: label (plain / info row / note tokens), pill button, step button, take row, selector (NSMenu popup), slider, N/A chip, wordmark, piano, pack list |
| `Source/UI/EditorView.mm` | The root view and `EditorController`: pages (CHORD / PACKS / INFO), the expiry gate, layout in the 800 x 650 design space, the 20 Hz refresh, licensing / updates / packs wiring, preset menu, take files |
| `Source/UI/TakeDrag.h/.mm` | OS drag of a take file (copy, generic, link and move offered, as FL Studio needs; outcome appended to `Takes/drag-log.txt`) |
| `Tools/EditorSnapshot.mm` | Offscreen renders of the 17 states / sizes the 0.5.1 tool rendered, plus before / after comparisons and a difference metric |
| `Tests/EditorTests.mm`, `Tests/EditorTests.cmake` | Control-by-control tests (target `ChordEngineEditorTests`) and the snapshot target |

All Objective-C classes carry the per-binary suffix (`CE_OBJC`), so the AU and
the VST3 (and two versions) can share one host process.

## Behaviour ported

- Clickable piano C2..C7 through `pushUiNoteOn/Off` (sliding across keys,
  black keys hit first); highlights from the processor's trigger / output
  state; the idle C4 preview tint.
- Chord readout (PREVIEW / PLAYING caption, chord symbol, note tokens).
- Preset selector (stock presets, then installed pack presets under their pack
  heading), previous / next with wrap, counter; Key, Scale.
- Transpose -/+ (-2..+2) and WHOLE / LOWEST / HIGHEST with the sand underline.
- Velocity mode; fixed-velocity slider dimmed and inert outside Fixed.
- Internal audio: switch + Synth / Piano / Bass in the builds with audio
  output; the N/A chip and the "load ChordEngine Instrument" line in the MIDI FX.
- MIDI recorder: RECORD ARM / CANCEL / STOP TAKE, status line, QUANTIZE TAKES,
  FOLLOW DAW RECORD, up to three take rows; dragging a row writes
  `<state>/Takes/ChordEngine Take N yyyymmdd-hhmmss.mid` (takes older than a
  week are pruned) and starts an OS drag; right-click / ctrl-click "Show in
  Finder".
- INFO: account rows, SIGN IN / SIGN OUT (one slot), OPEN MUSIC-PROD STUDIO
  (`musicprodstudio://open`), lifetime licence key field + ACTIVATE (cleared
  after use), the auth hint line, updates rows + CHECK FOR UPDATES
  (`UpdateService`, cached answer then one check per open), trial line, help
  & feedback.
- PACKS: catalog (fetched the first time the page opens), sign-in, refresh,
  per-pack actions (download / update / sign in / Music-Prod+ / built in).
- Expiry gate: replaces every page while an unlicensed trial has run out.
- The licensing state is the processor's (`processor().licensing()`); opening
  the editor calls `editorOpened()`.
- Scaling: the whole design scales with the view between 640 x 520 and
  1280 x 1040 (aspect kept, text on 0.5's gentler curve). A small resize grip
  sits in the bottom-right corner; in a VST3 it asks the host
  (`requestEditorSize`), in an AU the view resizes itself and the host window
  follows.
- Detach: `detachEditorView` / `detachAllEditorViews` stop the timer, release
  a held key and clear every control callback, so a view that outlives its
  plug-in instance still draws but never touches it.

## Visual comparison with 0.5.1

`Docs/Screenshots/native/` holds the new renders, `Docs/Screenshots/compare/`
shows 0.5.1 (left) next to the AppKit editor (right). Mean absolute
difference per channel (0..255) and the share of pixels with any channel off
by more than 24:

| Image | mean abs. diff | differing pixels |
|---|---|---|
| 800x650 | 0.39 | 0.30 % |
| 640x520 | 0.45 | 0.47 % |
| 1280x1040 | 0.44 | 0.36 % |
| 800x650-live | 0.39 | 0.30 % |
| 800x650-armed | 0.39 | 0.30 % |
| 800x650-recorder | 0.39 | 0.30 % |
| 640x520-recorder | 0.45 | 0.47 % |
| 1280x1040-recorder | 0.44 | 0.36 % |
| 800x650-info | 0.48 | 0.44 % |
| 640x520-info | 0.49 | 0.47 % |
| 1280x1040-info | 0.45 | 0.36 % |
| 800x650-info-activation | 0.62 | 1.00 % |
| 800x650-internal-audio | 0.39 | 0.30 % |
| 1280x1040-internal-audio-live | 0.44 | 0.36 % |
| 800x650-packs | 0.38 | 0.30 % |
| 640x520-packs | 0.38 | 0.31 % |
| 1280x1040-packs | 0.37 | 0.25 % |

Remaining differences, all small:

- The version strings differ (v0.5.1 / v0.6.0).
- The resize grip (new; needed so an AU window can be resized).
- Glyph rasterisation: CoreText vs JUCE's own shaping gives sub-pixel
  differences in kerning and antialiasing; the script wordmark sits about
  1-2 px higher.
- `-info-activation`: 0.5.1 captured ACTIVATE in its momentary pressed fill
  (a side effect of its click simulation); the AppKit render shows it at rest.

## Open items

- Editor tests do not click SIGN IN: the processor's licensing runtime always
  uses the real Music-Prod auth service, so a click would send a real
  `start` request. The INFO wiring is verified through ACTIVATE (refused
  locally while signed out) and the boundary-derived rows. A processor test
  seam for the auth service would allow it (reported to the main session).
- Keyboard focus rings (0.5.1 drew a sand ring on focused buttons) and
  keyboard navigation between controls are not ported; tooltips are.
- The selector popups are native NSMenus in dark appearance (0.5.1 drew its
  own menu in the panel colour); the closed selector draws exactly as before.
- Windows editor: not part of this step (see the main plan).
