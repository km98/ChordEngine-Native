# ChordEngine 0.5.0 UI redesign

Status: **second iteration ("editorial V2") implemented** in the native JUCE
editor and verified by rendered snapshots and the automated suite. Not yet
checked inside a DAW (see [Remaining verification](#remaining-verification)).

This document is the hand-off for anyone continuing the redesign, locally or
from a Claude Code Remote Control session. Everything it references lives in
this repository; every command runs from the repository root.

## Scope

A visual and layout redesign of the editor only. Unchanged: MIDI generation,
note handling, trigger ranges, presets and preset maps, scales, transpose
behaviour, processor state and serialization, MIDI recording and export,
licensing and lifetime activation, update logic and contracts, version 0.5.0,
plug-in identity (AU/VST3 codes, class ID, `Instrument` category), the
zero-audio-bus architecture, signing and packaging.

## History

1. **Original reference layout**: a port of the HISE reference. Blue accent,
   a card for every group, and a black logo slab.
2. **Iteration 1 (palette and structure)**: graphite, ivory and sand
   palette; a chord hero panel with a recorder card; a boxed preset, key and
   scale row; a framed piano; a filled bottom control strip; a two-column
   INFO dashboard inside a large card.

   Owner feedback: the colours are right, but the layout still read as the
   original's cards and rectangular control groups.
3. **Iteration 2 (editorial V2, current)**: the same palette, with a
   genuinely different composition, described below.

## Visual direction (V2)

Calm, precise, typographic. One open graphite page instead of nested panels.
Hierarchy comes from type size, weight and face. Relationships come from
alignment to a few shared column edges, with a hairline rule only where it
clarifies a boundary. The sand accent marks only the current page, the
active transpose mode, the primary choice (the preset), focus, the primary
action and the sounding chord.

| Token | Value | Use |
|---|---|---|
| `background` | `#0F0F11` | near-black ground outside the design canvas |
| `page` | `#161618` | the open working page (V2) |
| `inset` | `#131315` | faint wells (octave readout, licence-key field) |
| `raised` / `raisedHover` | `#232326` / `#2A2A2E` | pressed / hover fills |
| `hairline` / `border` | `#2A2A2E` / `#36363B` | rules / field underlines and outlines |
| `text` / `textBright` | `#ECE7DE` / `#F7F3EC` | ivory body / headline |
| `muted` / `dim` / `faint` | `#A6A096` / `#7C776E` / `#5A5650` | keys of rows / captions / footer |
| `accent` / `accentSoft` / `onAccent` | `#D8A55E` / `#3A3023` / `#1B1610` | sand accent / tinted press / text on sand |
| `terracotta` / `sage` | `#D06A50` / `#A3C39A` | trigger, record, error / confirmed |

`displayTone()` maps the licensing and update models' fixed status colours
onto this palette for display only. The model contracts are untouched.

### Typography

- **Display face** (the chord name and the INFO title): a book serif that
  ships with the operating system, never bundled. The first available of
  Baskerville (macOS), Georgia (macOS and Windows) and Times New Roman is
  used, falling back to the platform's default serif (`displayTypefaceName()`).
  Snapshots on the development Mac render in Baskerville; Windows will show
  Georgia.
- **Header wordmark** ("Chord Engine", top left): a script face that ships
  with the operating system, again never bundled (`wordmarkFace()` /
  `wordmarkFont()`):
  - **macOS:** Snell Roundhand Bold, a formal, legible roundhand. Its Bold cut
    is used so the hairlines hold at 640×520.
  - **Windows:** Gabriola (built in since Windows 7), the Windows
    calligraphic face.
  - **Fallback:** the italic of the display serif (Baskerville or Georgia
    Italic).

  Zapfino and Savoye were rejected as too ornamental. The word is drawn as
  live glyphs in ivory, about 27 pt at 800×650. Its ink, not its font box, is
  centred on the navigation's centre line (y = 36) and starts on the left
  edge (x = 40). The component carries the accessible title "Chord Engine".
  The Windows rendering has not been seen yet.
- **Text face**: the system sans, used for controls, notes and values.
- **Captions**: 8.5 pt bold capitals with 0.12 letter spacing.
- **Rows and values**: 11.5–12 pt.
- **Preset name**: 17 pt bold. Key and scale: 12.5 pt.
- **Notes**: 17 pt pitch class with an 11.5 pt octave figure.
- **INFO title**: 26 pt serif.
- **Chord name**: 54 pt serif.

**Responsive scale.** Geometry scales uniformly with the editor (one 800×650
design space, 640×520 to 1280×1040). Type does not:

- Text roles scale by `designScale^0.85`, so they are relatively larger at
  640×520 (legibility) and relatively smaller at 1280×1040 (air).
- The display role scales by `designScale^1.08`, so the chord name steps back
  on a small editor and gains presence on a large one.

### States

- **Hover:** a soft fill, or a brighter underline on fields.
- **Pressed:** a sand-tinted fill.
- **Disabled:** 45–50% alpha.
- **Keyboard focus:** a sand ring on buttons, a sand underline on fields.
- **Active navigation and transpose mode:** ivory text with a short sand
  underline. There is no segmented track.

## Layout (V2, 800×650 design space)

### CHORD page

| Region | Design bounds | Contents |
|---|---|---|
| Header | y 24–48, rule at y 60 | letter-spaced wordmark (x 40); `CHORD` / `INFO` text navigation (right), active page underlined in sand |
| Chord identity (`chordHeroBounds`) | (40, 72, 720 × 144) | caption (`PREVIEW - TRIGGER C4` / `PLAYING - TRIGGER <note>`), 54 pt serif chord symbol, the notes as a typeset sequence |
| Recorder column (`recorderCardBounds`) | (588, 80, 172 × 132), rule at x 568 | title, state line, three take rows (text rows with a sand `DRAG` arrow), full-width record action |
| Configuration | captions y 236, fields y 252 | CHORD PRESET name (17 pt bold, sand underline) with `05 / 12` and round previous/next buttons; KEY; SCALE. All underline-only fields |
| Keyboard | (40, 302, 720 × 180) | C2–C7 edge to edge on the page: a felt rail with a sand hairline, no bed or frame; C keys labelled |
| Secondary line (`outputStripBounds`) | (40, 494, 720 × 64), rule at y 494 | TRANSPOSE (round −/+ around the octave readout; WHOLE / LOWEST / HIGHEST as words) \| VELOCITY MODE (underline field) and FIXED VELOCITY \| INTERNAL AUDIO (N/A) |
| Foot | y 600–628 | Music-Prod mark (ivory, 70%); `Chord Engine v0.5.0` |

**Identity states:**

- **Preview and live share one composition.** The idle headline is now the
  C4 preview chord's own symbol (resolved by the Core with
  `ChordPresetSystem::resolve`, exactly as the live readout). Playing a
  chord only changes the caption and turns the symbol and pitch classes sand.
- **No repeated context line.** The previous idle line
  (`<Key> <Scale> - <Preset>`) repeated the configuration fields directly
  below it, so it is no longer shown.
- **Recorder:** waiting shows a one-line explanation of the workflow; armed
  turns the state line sand and the action becomes an outlined CANCEL;
  recording makes the action terracotta (STOP TAKE); stored takes are list
  rows.

### INFO page

An editorial document rather than a dashboard: section names in a 136 px
label column (x 40), content on one shared edge (x 192), and a rule between
sections. There are no cards.

| Section | Design rows | Contents |
|---|---|---|
| Identity | y 78–134, rule at y 146 | `Chord Engine` (26 pt serif), `Version v0.5.0` |
| ACCOUNT | y 158–270, rule at y 284 | User, Music-Prod+ and License rows (key column and value; License may wrap to two lines); SIGN IN (sand) or SIGN OUT; OPEN MUSIC-PROD STUDIO |
| LIFETIME LICENCE KEY | y 298–380, rule at y 390 | key field (monospaced, underline) and ACTIVATE (sand outline) on one line; a three-line result area beneath (device link, activation errors, device limits) |
| UPDATES / TRIAL | y 402–512, rule at y 524 | Current / Latest / Status rows (quiet, 11.5–12 pt), CHECK FOR UPDATES on the right; Trial access row |
| HELP & FEEDBACK (`infoHelpPanelBounds`) | (40, 532, 720 × 42) | one line of copy and OPEN HELP & FEEDBACK |

### Expiry gate

The same opaque gate and the same wording, now typographic: a letter-spaced
`ACCESS EXPIRED` title, a short terracotta rule, the explanation, then OPEN
MUSIC-PROD STUDIO (sand) and SIGN IN (outlined). The navigation stays visible
above it.

## Source changes

- `Source/PluginEditor.h` and `Source/PluginEditor.cpp`:
  - **Composition:** `paint()`, `paintChordPage()`, `paintInfoPage()`,
    `drawLogo()` and `resized()` were rewritten for V2. The region accessors
    (`chordHeroBounds`, `outputStripBounds`, `recorderCardBounds`,
    `infoBedBounds` and `infoHelpPanelBounds`) now describe open regions.
  - **Typography:** `displayTypefaceName()` / `displayFont()`; the
    `TextSpec::role` (text or display) and `textScale()` responsive scaling.
    Labels have no inner padding, so all text aligns to the column edges.
  - **Look and feel:** underline-only selectors and the licence-key field
    (`fillTextEditorBackground`, `drawTextEditorOutline`).
  - **Buttons:** round, quiet `StepButton`s. `PillButton::setIndicator()`
    adds the record-state dot. Navigation and transpose mode are text buttons
    with a painted underline.
  - **Labels:** `InfoRowLabel` uses a fixed key column; `NoteTokensLabel`
    draws the typeset note sequence.
  - **Other components:** take rows as list rows; the unframed keyboard.
  - **Readout:** the idle headline is the preview chord's symbol (display
    only).
  - **Kept as they were:** every component ID, action, callback and model
    binding.
- `Tools/GuiSnapshot.cpp`: an added `-armed` render (recorder armed).
- `Tests/FunctionalTests.cpp`:
  - Case 21's licence-row assertion now matches the stacked layout.
  - New case 22 covers first open.
  - Cases 17, 19 and 20 are unchanged.

## Rendering the preview

```sh
cmake -S . -B Build/macOS-JUCE9 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build Build/macOS-JUCE9 --config Release --target ChordEngineGuiSnapshot
./Build/macOS-JUCE9/Release/ChordEngineGuiSnapshot
```

Any build directory works (`-B <dir>`); the PNGs always land in
`GuiSnapshots/` next to the executable. The tool:

- needs no arguments, window, host or network;
- uses a throwaway licensing and update state folder (never the user's real
  Music-Prod state) and the offline update transport.

| File | State |
|---|---|
| `ChordEngine-GUI-800x650.png`, `-640x520.png`, `-1280x1040.png` | CHORD page, idle |
| `ChordEngine-GUI-800x650-live.png` | C4 held: live readout and keyboard highlights |
| `ChordEngine-GUI-800x650-armed.png` | recorder armed |
| `ChordEngine-GUI-800x650-recorder.png` | one stored take; Fixed velocity enabled |
| `ChordEngine-GUI-{800x650,640x520,1280x1040}-info.png` | INFO page, signed out, fresh trial |
| `ChordEngine-GUI-800x650-info-activation.png` | INFO after ACTIVATE while signed out (result line) |

## Build and test

```sh
cmake -S . -B Build/macOS-JUCE9 -G Xcode \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build Build/macOS-JUCE9 --config Release
ctest --test-dir Build/macOS-JUCE9 -C Release --output-on-failure
git diff --check
```

Editor layout and behaviour coverage in `ChordEngineFunctionalTests`:

- **Case 16:** the keyboard's 61-key geometry and real note path.
- **Case 17:** RECORD ARM inside, and centred in, the recorder column at all
  three sizes.
- **Case 19:** the licence row inside the INFO content and clear of the help
  line.
- **Case 20:** the CHORD page at all three sizes:
  - identity, recorder and secondary-line containment;
  - no overlaps among 29 controls;
  - practical hit areas;
  - preset browsing, including wrap at both ends and the counter text.
- **Case 21:** the INFO page at all three sizes:
  - containment and no overlaps;
  - the help line;
  - the key field and ACTIVATE on one line, between the account actions and
    UPDATES;
  - the activation result line under the field with multi-line room.
- **Case 22:** first open:
  - the constructor alone lays out and typesets the editor;
  - the chord name is 54 pt in the display face;
  - captions are bold and letter-spaced;
  - a resize cycle reproduces identical bounds and type.

The INTERNAL AUDIO note ("MIDI effect - no audio bus") has
`setMinimumHorizontalScale(1.0f)`, so it wraps onto two lines at every size.
At 1280 x 1040 it previously nearly fit on one line, and JUCE squeezed the
glyphs horizontally instead of wrapping.

## Remaining verification

- **Not checked in a real DAW:** Retina or HiDPI host scaling, Logic's AU
  window, other VST3 hosts.
- **Fonts on other systems:** Windows will render the chord name in Georgia
  rather than Baskerville. That fallback has not been rendered.
- **Popups and tooltips:** the combo-box popup menus and tooltips use the new
  look and feel but cannot be captured by the snapshot tool.
- **Licensing states not rendered:** signed in, Music-Prod+ active, licence
  active, update available, expired trial and the expiry gate need real or
  injected licensing state. Their rows are covered by the layout tests
  (wrapping inside fixed bounds) but have not been seen.
- **Recorder states not rendered:** RECORDING (STOP TAKE) and three stored
  takes. Only waiting, armed and one stored take are captured.
- **Hover and keyboard focus:** both are painted, but no snapshot shows them.
- **Superseded docs:** `Docs/SECOND_MAC_GUI_PARITY_TEST.md` and
  `Docs/NATIVE_GUI_REFERENCE_INVENTORY.md` describe the pre-redesign
  reference geometry. They remain as the historical parity record.
