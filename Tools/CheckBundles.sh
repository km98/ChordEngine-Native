#!/bin/bash
# =============================================================================
# CheckBundles.sh - the built bundles' metadata, as hosts and installers see it.
#
#   Tools/CheckBundles.sh <version> <MIDI FX .component> <Instrument .component> <.vst3>
#
# Per bundle: CFBundleIdentifier (the 0.6 unique ids), version strings, package
# type BNDL, LSMinimumSystemVersion 12.0, Contents/PkgInfo = "BNDL????", a
# universal arm64 + x86_64 executable built for macOS 12.0 (minos), only the
# format's entry points exported, and a strictly valid (ad hoc) signature.
# AU: the AudioComponents entry equals 0.5.1's identity (type, subtype,
# manufacturer, name, factory function) and its version is
# major << 16 | minor << 8 | patch. VST3: moduleinfo.json is valid JSON with
# 0.5.1's class ID.
# =============================================================================
set -u
export LC_ALL=C
[ $# -eq 4 ] || { echo "usage: $0 <version> <MIDI FX .component> <Instrument .component> <.vst3>" >&2; exit 2; }
VERSION="$1"; MIDIFX="$2"; INSTRUMENT="$3"; VST3="$4"
failures=0
check() { if [ "$1" = "$2" ]; then echo "PASS: $3"; else echo "FAIL: $3 (got '$1', expected '$2')"; failures=$((failures + 1)); fi; }
plist() { /usr/libexec/PlistBuddy -c "Print :$2" "$1/Contents/Info.plist" 2>/dev/null; }

IFS=. read -r MAJOR MINOR PATCH <<< "$VERSION"
AU_VERSION=$(( (MAJOR << 16) | (MINOR << 8) | PATCH ))

common() {
    local bundle="$1" id="$2" exe_name exe
    local name; name=$(basename "$bundle")
    check "$(plist "$bundle" CFBundleIdentifier)" "$id" "$name: bundle id"
    check "$(plist "$bundle" CFBundleShortVersionString)" "$VERSION" "$name: short version"
    check "$(plist "$bundle" CFBundleVersion)" "$VERSION" "$name: bundle version"
    check "$(plist "$bundle" CFBundlePackageType)" "BNDL" "$name: package type"
    check "$(plist "$bundle" LSMinimumSystemVersion)" "12.0" "$name: minimum macOS"
    check "$(cat "$bundle/Contents/PkgInfo" 2>/dev/null)" "BNDL????" "$name: Contents/PkgInfo"
    exe_name=$(plist "$bundle" CFBundleExecutable)
    exe="$bundle/Contents/MacOS/$exe_name"
    check "$(lipo -archs "$exe" 2>/dev/null | tr ' ' '\n' | sort | tr '\n' ' ')" "arm64 x86_64 " "$name: universal"
    for arch in arm64 x86_64; do
        minos=$(otool -arch "$arch" -l "$exe" | awk '/LC_BUILD_VERSION/{f=1} f&&/minos/{print $2; exit}')
        check "$minos" "12.0" "$name [$arch]: built for macOS 12.0"
    done
    grep -q "Apache License" "$bundle/Contents/Resources/THIRD_PARTY_NOTICES.txt" 2>/dev/null \
        && grep -q "MIT License" "$bundle/Contents/Resources/THIRD_PARTY_NOTICES.txt"
    check "$?" "0" "$name: ships THIRD_PARTY_NOTICES.txt (Apache-2.0 + MIT)"
    codesign --verify --deep --strict "$bundle" 2>/dev/null
    check "$?" "0" "$name: strict signature verification"
}

exports() { nm -gU -arch arm64 "$1" | awk '{print $3}' | sort | tr '\n' ' '; }

au() {
    local bundle="$1" id="$2" type="$3" subtype="$4" name="$5" factory="$6"
    common "$bundle" "$id"
    local label; label=$(basename "$bundle")
    check "$(plist "$bundle" AudioComponents:0:type)" "$type" "$label: AU type"
    check "$(plist "$bundle" AudioComponents:0:subtype)" "$subtype" "$label: AU subtype"
    check "$(plist "$bundle" AudioComponents:0:manufacturer)" "Musc" "$label: AU manufacturer"
    check "$(plist "$bundle" AudioComponents:0:name)" "$name" "$label: AU name"
    check "$(plist "$bundle" AudioComponents:0:factoryFunction)" "$factory" "$label: AU factory"
    check "$(plist "$bundle" AudioComponents:0:version)" "$AU_VERSION" "$label: AU version = major<<16|minor<<8|patch"
    check "$(exports "$bundle/Contents/MacOS/$(plist "$bundle" CFBundleExecutable)")" "_$factory " "$label: exports only the factory"
}

au "$MIDIFX" com.musicprod.chordengine.au aumi Cefl "Music-Prod: ChordEngine" ChordEngineAUFactory
au "$INSTRUMENT" com.musicprod.chordengine.instrument.au aumu Cein "Music-Prod: ChordEngine Instrument" ChordEngine_InstrumentAUFactory

common "$VST3" com.musicprod.chordengine.vst3
check "$(exports "$VST3/Contents/MacOS/ChordEngine")" "_GetPluginFactory _bundleEntry _bundleExit " "ChordEngine.vst3: exports only the VST3 entry points"
cid=$(python3 -c 'import json,sys; d=json.load(open(sys.argv[1])); print(d["Classes"][0]["CID"], d["Classes"][0]["Version"])' \
      "$VST3/Contents/Resources/moduleinfo.json" 2>/dev/null)
check "$cid" "ABCDEF019182FAEB4D7573634365666C $VERSION" "ChordEngine.vst3: moduleinfo.json is JSON with 0.5.1's class ID"

echo "CheckBundles: $failures failure(s)"
[ "$failures" -eq 0 ]
