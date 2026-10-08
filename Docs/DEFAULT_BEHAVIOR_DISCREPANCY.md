# Default velocity behavior — product brief vs embedded FL reference

**Status: RESOLVED FOR NATIVE GUI MILESTONE — Dynamic / 100 selected.**

The initial product brief's Fixed/100 statement conflicts with the approved embedded FL
reference's Dynamic/100 behavior. For this milestone, the user explicitly instructed
that artifact-backed reference behavior takes precedence unless a newer authoritative
specification is provided. No newer specification was provided, so the native GUI uses
**Dynamic / 100** for both AU and VST3. This records a scoped product decision; it does
not erase the earlier brief's provenance or claim that the original conflict never
existed.

## The conflict

| Claim | Default velocity mode | Fixed velocity value |
|---|---|---|
| **PRODUCT BRIEF** | `Fixed` | `100` |
| **EMBEDDED FL REFERENCE** | `Dynamic` | `100` |

Both statements agree that the fixed-velocity value is **100**. They disagree only on
which **mode** is active in a fresh instance.

## Exact source of each claim

### PRODUCT BRIEF — Fixed / 100

- Origin: the product/task specification for the native rewrite, stated directly in the
  development instructions for this work (earlier milestone phase: "Reference defaults:
  Fixed, velocity 100"; current milestone: "PRODUCT BRIEF: Fixed / 100").
- The brief is a **forward-looking product intent** statement. It is not backed by any
  file inside this repository: no repository document, fixture, or source file records a
  Fixed-mode default. Every in-repository document that discusses the reference says
  Dynamic (see below).
- The brief does not state which product version or artifact it describes, and it does
  not say whether "Fixed / 100" is the default of an existing shipped build or the
  desired default of the native rewrite.

### EMBEDDED FL REFERENCE — Dynamic / 100

- Origin: the script embedded in the **approved ChordEngine FL 0.4.0 VST3**
  (embedded main script SHA-256
  `5ba711582e87c75b4c38322152055b12bc6ef6c92dc9ba6444d1cbda34b660da`),
  extracted and analyzed read-only during the reference-capture milestone.
- Recorded in [FL_REFERENCE_BEHAVIOR_CAPTURE.md](FL_REFERENCE_BEHAVIOR_CAPTURE.md):
  - Section 1: "Velocity mode default is Dynamic. Dynamic uses input velocity; Maximum
    sets 127; Fixed uses the fixed setting (default 100, UI range 1–127, step 1)"
    (embedded script lines 826–875, 1081–1083, 5403–5425, 5640–5669).
  - Section 6: "Factory initialization selects C / Minor / Dreamy ... Velocity mode
    defaults Dynamic and the displayed fixed-velocity value is 100" (lines 1087–1089,
    5016–5110, 5139–5177).
- Recorded in [FLReferenceVectors.json](../Tests/FLReferenceVectors.json)
  (`evidenceClass: VERIFIED-SOURCE-STATIC`):

  ```json
  "defaultStateSource": {
    "velocityMode": "dynamic",
    "fixedVelocity": 100
  }
  ```

- Recorded in [REFERENCE_FUNCTIONAL_INVENTORY.md](REFERENCE_FUNCTIONAL_INVENTORY.md)
  items 24–25 (editable reference source): velocity combo defaults to Dynamic, fixed
  velocity 100.
- Evidence class: **source-static only**. No DAW/host session has observed either
  default at runtime (`hostObserved: false` throughout the fixture).

## Exact current native default

- The native GUI milestone retains the previously existing Core default; no Core default
  change was made while implementing the editor.
- Declared in [ChordEngineCore.h](../Source/Core/ChordEngineCore.h):

  ```cpp
  VelocityMode velocityMode = VelocityMode::dynamic;   // Dynamic
  std::uint8_t  fixedVelocity = 100;                   // 100
  ```

- The processor initializes its lock-free in-memory configuration snapshot from the
  existing core configuration, so a fresh plugin instance starts in **Dynamic / 100**.
  The native GUI shows Dynamic
  selected and the fixed value 100 disabled until Fixed mode is selected.
- This default is asserted by tests:
  - [CoreTests.cpp](../Tests/CoreTests.cpp) — "core defaults match embedded release
    source, including Dynamic mode".
  - [ProcessorIntegrationTests.cpp](../Tests/ProcessorIntegrationTests.cpp) — the
    processor probe requires `velocityMode == dynamic && fixedVelocity == 100` and
    prints a pointer to this document if it ever changes.
- All other native defaults match both claims: key C, scale Minor, preset Dreamy,
  transpose WHOLE 0.

## Exact reference default

The approved FL 0.4.0 artifact's factory initialization: **Dynamic / 100**
(`defaultStateSource` in `Tests/FLReferenceVectors.json`, corroborated by
`Docs/FL_REFERENCE_BEHAVIOR_CAPTURE.md` sections 1 and 6).

## Decision record for native GUI milestone

- **Decision:** use artifact-backed **Dynamic / 100**; apply consistently to AU and VST3.
- **Authority:** explicit user direction in the GUI milestone: “For this milestone,
  use the artifact-backed reference behavior: Dynamic / 100 unless the user has
  explicitly provided a newer authoritative product specification.”
- **Implementation effect:** no core default change; the UI exposes the existing
  Dynamic / 100 core default. There is no hidden alternate default.
- **Scope:** this closes the discrepancy for the current native development GUI
  milestone. If a future authoritative product specification changes the intended
  default, treat that as a separately documented product change and update tests and
  reference-parity claims then.

## Why this required an explicit decision

1. **The two authorities conflict.** The initial product brief said Fixed/100, while
   the approved embedded artifact shows Dynamic/100. The current user's milestone
   instruction explicitly selects artifact-backed Dynamic/100 for the native GUI.
2. **The choice is observable.** With Dynamic, generated note velocity follows the
   played velocity (e.g. 1 → 1, 96 → 96); with Fixed it is always 100. A manual host
   test of velocity behavior exposes the selected default.
3. **It changes test and documentation truth.** Switching to Fixed would contradict
   current native tests, reference docs, and the `sourceDerived` fixture. The explicit
   user decision resolves this milestone while retaining the earlier brief as provenance.
4. **It cannot be inferred from the reference artifact.** The approved 0.4.0 VST3
   initializes Dynamic; it contains no state that could express a Fixed/100 default.
5. **Parity claims depend on it.** "Behavioral parity with the captured reference"
   currently implies Dynamic. A Fixed default would be an intentional, documented
   divergence from 0.4.0, not parity.

The discrepancy is now closed for this milestone as recorded above. Keep the
source-derived vector fixture marked `hostObserved: false`; the decision is about the
native development default, not a claim that the embedded reference was host-captured.
