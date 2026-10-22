# AGENTS.md — ChordEngine Native

Persistent instructions for every AI agent (Freebuff or otherwise) working in this
repository. Read this file before making changes.

- **Repository root:** `~/Documents/ChordEngine-Native`
- **Default branch:** `main`
- **Remote:** `origin` = `git@github.com:km98/ChordEngine-Native.git`

Do not confuse this project with `~/Music/Logic` (a Logic Pro project folder holding
`.logicx` bundles, not a repository) or any legacy HISE/SideChainer checkout. This
native CMake/JUCE repository is the ChordEngine project.

The sections marked MANDATORY are binding for every agent session in this repo.

---

## Other project conventions

- Do not commit, push, tag or release unless the user asks for it in that session.
- Keep build output out of the repository (`/Build/*` is git-ignored except
  `.gitkeep`); build in-tree only through CMake as documented in `Docs/`.
- Prefer the documented build/test flow in `Docs/TEST_STRATEGY.md` and
  `Docs/BUILD_VERIFICATION.md` over ad-hoc commands.

## Local and Claude Code Remote Control sessions (MANDATORY)

The project must stay equally workable from a local Claude Code terminal session and
from a Remote Control session (claude.ai / Claude app) attached to the development Mac.
Both use this repository in place as the single working directory.

- Work in the repository at its existing location. Never move or copy the project
  into `/tmp`, a temporary worktree or another machine-specific directory.
- Every source change, test, doc and redesign note needed to resume work must be in
  the repository. Record redesign status and decisions under `Docs/`, not only in
  chat history.
- `/tmp` (or the session scratchpad) may hold throwaway builds, screenshots, backups
  and diagnostics, but nothing required to open, build, test or continue the project
  may depend on it.
- Use repository-relative paths in source, CMake, scripts and new docs. Do not add
  absolute or user-specific paths without a documented reason. (Absolute paths in
  older dated verification records are historical evidence; leave them as is.)
- Do not depend on the current Terminal session, exported shell variables, manually
  opened windows or long-running ad-hoc processes. Every step must be reproducible
  from the repository root with the commands below.
- Never add credentials, API keys or other secrets to the repository or to Claude /
  Remote Control configuration. Do not enable Remote Control globally or change
  unrelated Claude settings.
- Preserve existing uncommitted and untracked work; check `git status` before and
  after a session.

Session-independent workflow, run from the repository root. ChordEngine 0.6 (branch
`juce-free`) contains no JUCE: the only third-party code is the vendored Apple
AudioUnitSDK and Steinberg VST 3 SDK in `ThirdParty/`. Needs `cmake` ≥ 3.22 on `PATH`
and Xcode (C++23); `Tools/FetchSdk.sh` fetches the full VST 3 SDK and Steinberg's
validator (test tools only) into `Build/sdk-src/` and needs network access once:

```sh
Tools/FetchSdk.sh
cmake -S . -B Build/native -G "Unix Makefiles" \
  -DCMAKE_OSX_ARCHITECTURES='arm64;x86_64' -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON
cmake --build Build/native -j 8
ctest --test-dir Build/native --output-on-failure

# GUI preview without a DAW: renders the real AppKit editor offscreen to PNGs
# (no window, no host); add <0.5.1 dir> <compare dir> for before/after images.
./Build/native/Tests/ChordEngineEditorSnapshot Build/native/EditorSnapshots
```

Tests that touch per-user folders run with `CFFIXED_USER_HOME` set by CTest (setting
`HOME` does not redirect them), so they never read or write real licence, pack or
update files. Never install test builds into `/Library/Audio/Plug-Ins` or
`~/Library/Audio/Plug-Ins`; the AU and VST3 host tests load the built bundles
in-process. Bundles are in `Build/native/Bundles/`, sealed ad hoc only. Packaging:
`Tools/PackageMacDMG.sh --unsigned` checks the installer structure without signing;
signing, notarizing and releasing are the owner's. Architecture, identity, test
results and open items: `Docs/NATIVE_REBUILD.md`; editor: `Docs/UI_NATIVE_EDITOR.md`.

In a Remote Control session, review the snapshot PNGs by reading them from that
folder. The current UI redesign (visual system, layout, snapshot states,
tests and open items) is documented in `Docs/UI_REDESIGN_0.5.0.md`. Logic/second-Mac host checks in `Docs/` remain manual, local-only steps.
