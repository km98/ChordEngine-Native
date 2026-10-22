#!/bin/bash
# Fetches the FULL upstream SDKs at the pinned tags into Build/sdk-src/ for the
# test tools that need more than the vendored subsets in ThirdParty/: the VST3
# hosting helpers (Tests/VST3HostTest) and Steinberg's validator. Nothing
# fetched here ships. Re-running is a no-op when the pinned commits are present.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
DEST="$ROOT/Build/sdk-src"
VST3_TAG="v3.8.0_build_66"
VST3_COMMIT="9fad9770f2ae8542ab1a548a68c1ad1ac690abe0"
mkdir -p "$DEST"
if [ ! -d "$DEST/vst3sdk/.git" ]; then
    git clone -q --depth 1 --branch "$VST3_TAG" --recurse-submodules --shallow-submodules \
        https://github.com/steinbergmedia/vst3sdk.git "$DEST/vst3sdk"
fi
actual="$(git -C "$DEST/vst3sdk" rev-parse HEAD)"
[ "$actual" = "$VST3_COMMIT" ] || { echo "vst3sdk is at $actual, expected $VST3_COMMIT" >&2; exit 1; }
# Steinberg's validator (a test tool).
if [ ! -x "$DEST/vst3sdk-build/bin/Release/validator" ]; then
    cmake -S "$DEST/vst3sdk" -B "$DEST/vst3sdk-build" -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Release \
        -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF -DSMTG_ADD_VSTGUI=OFF -DSMTG_ENABLE_VST3_PLUGIN_EXAMPLES=OFF \
        -DSMTG_ENABLE_VST3_HOSTING_EXAMPLES=OFF -DSMTG_RUN_VST_VALIDATOR=OFF -DSMTG_CREATE_PLUGIN_LINK=OFF >/dev/null
    cmake --build "$DEST/vst3sdk-build" --target validator -j 8 >/dev/null
fi
echo "vst3sdk $VST3_TAG ($VST3_COMMIT) and its validator are in $DEST"
