#!/bin/bash
# =============================================================================
# CheckNoJuce.sh - fails if JUCE comes back into ChordEngine.
#
#   Tools/CheckNoJuce.sh <repo root> [<bundle> ...]
#
# Sources (always):
#   1. No framework code anywhere in Source/, Tests/, Tools/, ThirdParty/ or
#      the CMake files: no juce:: identifiers, juce headers, JUCE_ macros,
#      JucePlugin_ definitions, juce_add_ CMake calls or a JUCE FetchContent.
#   2. Product sources (Source/) do not mention it at all, except the two
#      state-format keys 0.5.x projects require: "jucePluginState" (the AU
#      ClassInfo key) and "JUCEPrivateData" (the VST3 state trailer marker).
# Binaries (for each bundle given):
#   3. No symbol whose name contains "juce" (any case) in any architecture.
#   4. Only system libraries are linked (/System/Library, /usr/lib).
#      Windows VST3 bundles (Contents/x86_64-win): the DLL imports only
#      Windows and Microsoft C/C++ runtime DLLs and exports only the VST3
#      entry points (both read with dumpbin, passed as $DUMPBIN).
#   5. The only strings containing "juce" are the two state-format keys (on
#      x86_64 the compiler may store a key as immediate data, so `strings`
#      sees a fragment such as "JUCEPriv" followed by instruction bytes; a
#      string beginning with an 8-byte prefix of a key counts as that key).
# Exit 0 = clean, 1 = JUCE found, 2 = usage.
# =============================================================================
set -u
ROOT="${1:-}"
[ -n "$ROOT" ] && [ -d "$ROOT/Source" ] || { echo "usage: $0 <repo root> [<bundle> ...]" >&2; exit 2; }
shift
failures=0
fail() { echo "FAIL: $*"; failures=$((failures + 1)); }
pass() { echo "PASS: $*"; }

# 1. framework code patterns
pattern='juce::|#[[:space:]]*include[[:space:]]*[<"]juce|JUCE_[A-Z][A-Z_]*|JucePlugin_|juce_add_|ScopedJuce|juce_[a-z_]+\.h|FetchContent_Declare\([[:space:]]*JUCE'
hits=$(cd "$ROOT" && grep -rnIE "$pattern" Source Tests Tools ThirdParty CMakeLists.txt Tests/CMakeLists.txt \
        --exclude=CheckNoJuce.sh 2>/dev/null)
if [ -n "$hits" ]; then
    fail "JUCE code in the sources:"; echo "$hits" | head -40
else
    pass "no JUCE identifiers, headers, macros or CMake in Source/, Tests/, Tools/, ThirdParty/, CMake"
fi

# 2. product sources mention nothing but the two state keys
hits=$(cd "$ROOT" && grep -rniI "juce" Source | grep -v "jucePluginState" | grep -v "JUCEPrivateData")
if [ -n "$hits" ]; then
    fail "Source/ mentions JUCE beyond the two state-format keys:"; echo "$hits" | head -40
else
    pass "Source/ mentions JUCE only as the two state-format keys"
fi

# 3-5. binaries
windows_bundle() {
    local bundle="$1" name dll imports bad exports other
    name=$(basename "$bundle")
    dll="$bundle/Contents/x86_64-win/$name"
    [ -f "$dll" ] || { fail "$name: no x86_64-win binary"; return; }
    [ -n "${DUMPBIN:-}" ] || { fail "$name: DUMPBIN is not set"; return; }
    imports=$("$DUMPBIN" -nologo -imports "$dll" | tr -d '\r' | awk '/^ +[A-Za-z0-9_.-]+\.dll$/{print $1}' | sort -u)
    bad=$(echo "$imports" | grep -viE '^(kernel32|user32|gdi32|advapi32|shell32|ole32|oleaut32|winhttp|bcrypt|shlwapi|vcruntime140(_1)?|msvcp140(_[0-9a-z]+)?|api-ms-win-[a-z0-9-]+)\.dll$')
    [ -n "$imports" ] && [ -z "$bad" ] && pass "$name [x64]: imports Windows/MSVC runtime DLLs only ($(echo $imports | tr ' ' ','))" \
                                      || fail "$name [x64]: unexpected imports: $bad"
    exports=$("$DUMPBIN" -nologo -exports "$dll" | tr -d '\r' | awk '$1 ~ /^[0-9]+$/ && $2 ~ /^[0-9A-F]+$/ && $3 ~ /^[0-9A-F]+$/ && NF >= 4 {print $4}' | sort | tr '\n' ' ')
    [ "$exports" = "ExitDll GetPluginFactory InitDll " ] && pass "$name [x64]: exports only the VST3 entry points" \
                                                        || fail "$name [x64]: exports '$exports'"
    other=$(grep -a -o -i -E '[[:alnum:]_:]*juce[[:alnum:]_:]*' "$dll" | grep -vE '^(jucePluginState|JUCEPrivateData)$' | sort -u)
    [ -z "$other" ] && pass "$name [x64]: the only 'juce' strings are the state-format keys" \
                    || fail "$name [x64]: unexpected strings: $(echo "$other" | head -5 | tr '\n' ' ')"
}

for bundle in "$@"; do
    name=$(basename "$bundle")
    if [ -d "$bundle/Contents/x86_64-win" ]; then windows_bundle "$bundle"; continue; fi
    exe="$bundle/Contents/MacOS/$(/usr/libexec/PlistBuddy -c 'Print :CFBundleExecutable' "$bundle/Contents/Info.plist" 2>/dev/null)"
    [ -f "$exe" ] || { fail "$name: no executable"; continue; }
    for arch in $(lipo -archs "$exe"); do
        count=$(nm -arch "$arch" -a "$exe" 2>/dev/null | grep -ci "juce")
        [ "$count" -eq 0 ] && pass "$name [$arch]: 0 JUCE symbols" || fail "$name [$arch]: $count JUCE symbols"
        libs=$(otool -arch "$arch" -L "$exe" | tail -n +2 | awk '{print $1}' | grep -vE '^(/System/Library/|/usr/lib/)')
        [ -z "$libs" ] && pass "$name [$arch]: system libraries only" || fail "$name [$arch]: non-system libraries: $libs"
        other=$(strings -a -arch "$arch" "$exe" | grep -i "juce" | grep -vE '^(jucePluginState|JUCEPrivateData)$' \
                | grep -vE '^(jucePlug|JUCEPriv)')
        [ -z "$other" ] && pass "$name [$arch]: the only 'juce' strings are the state-format keys" \
                        || fail "$name [$arch]: unexpected strings: $(echo "$other" | head -5 | tr '\n' ' ')"
    done
done

echo "CheckNoJuce: $failures failure(s)"
[ "$failures" -eq 0 ]
