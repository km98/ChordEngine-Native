#!/bin/bash
# ============================================================================
# PackageMacDMG.sh
#
# Builds the customer-facing macOS installer DMG for the ChordEngine native
# (JUCE) build. It carries BOTH plugin formats in a single installer:
#
#     /Library/Audio/Plug-Ins/Components/ChordEngine.component   (AU, aumi)
#     /Library/Audio/Plug-Ins/VST3/ChordEngine.vst3              (VST3, Fx)
#
# The payload is taken from the already-built universal Release artefacts;
# this script never builds them. It mirrors the conventions proven by the
# SideChainer / ChordEngine FL release flow:
#
#   1. stage the bundles under a pkg root with 0755 directories
#   2. codesign both bundles with the Developer ID APPLICATION identity,
#      hardened runtime + secure timestamp (required for notarization)
#   3. pkgbuild with --ownership recommended, gate the recorded modes in the
#      BOM (a previous 0700 leak made the VST3 invisible to DAWs)
#   4. productsign with the Developer ID INSTALLER identity
#   5. notarytool submit --wait, then staple the ticket
#   6. wrap the stapled .pkg plus README.txt in a compressed DMG
#   7. notarize + staple the DMG as well, then mount and re-verify it
#
# Nothing in the build tree is modified: the bundles are copied with `ditto`
# into a temporary staging root and signed there. Build outputs keep the
# ad-hoc signature the CMake/Xcode build produces.
#
# Usage:
#   ./Tools/PackageMacDMG.sh [--skip-notarize] [--force] [--version X.Y.Z]
#
# Exit status is non-zero on any gate failure, and no artifact is published
# (copied into the output directory) unless every gate before it passed.
# ============================================================================
set -euo pipefail
umask 022

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

# ---------------------------------------------------------------- parameters
BUILD_DIR="${CHORDENGINE_BUILD_DIR:-$REPO_ROOT/Build/macOS-JUCE9}"
OUT_DIR="${CHORDENGINE_DIST_DIR:-$REPO_ROOT/Build/ReleaseArtifacts}"

AU_SRC="$BUILD_DIR/ChordEngineAU_artefacts/Release/AU/ChordEngine.component"
VST3_SRC="$BUILD_DIR/ChordEngineVST3_artefacts/Release/VST3/ChordEngine.vst3"

PRODUCT="ChordEngine"
VERSION="0.1.0"
PKG_IDENTIFIER="com.musicprod.chordengine.pkg"

APP_ID="Developer ID Application: Martin Kadziolka (3A4R5EKM7V)"
INSTALLER_ID="Developer ID Installer: Martin Kadziolka (3A4R5EKM7V)"
NOTARY_PROFILE="${NOTARY_PROFILE:-ChordEngineFL-notary}"

DO_NOTARIZE=1
FORCE=0

die()  { echo "ERROR: $*" >&2; exit 1; }
step() { echo; echo "== $* =="; }
sha256of() { shasum -a 256 "$1" | awk '{print $1}'; }

# notary_status <json-log> <human-description> — fail closed unless Accepted.
notary_status() {
    local status
    status="$(python3 -c 'import json,sys; print(json.load(open(sys.argv[1])).get("status", ""))' \
                "$1" 2>/dev/null || true)"
    echo "  notary status: ${status:-<unparseable>}"
    [ "$status" = "Accepted" ] || die "$2 was not Accepted by the notary service"
}

usage() { sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'; exit "${1:-0}"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --skip-notarize) DO_NOTARIZE=0; shift ;;
        --force)         FORCE=1; shift ;;
        --version)       VERSION="${2:?--version needs a value}"; shift 2 ;;
        -h|--help)       usage 0 ;;
        *) die "unknown argument: $1 (try --help)" ;;
    esac
done

VOL_NAME="$PRODUCT $VERSION"
PKG_NAME="$PRODUCT-$VERSION-macOS.pkg"
DMG_NAME="$PRODUCT-$VERSION-macOS.dmg"

[[ "$VERSION" =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || die "invalid version: $VERSION"

# ---------------------------------------------------------------- preflight
step "Preflight"
echo "repo        : $REPO_ROOT"
echo "build dir   : $BUILD_DIR"
echo "output dir  : $OUT_DIR"
echo "version     : $VERSION"
echo "notarize    : $DO_NOTARIZE (profile: $NOTARY_PROFILE)"

[ -d "$AU_SRC" ]  || die "AU bundle not found: $AU_SRC"
[ -d "$VST3_SRC" ] || die "VST3 bundle not found: $VST3_SRC"

[ -d "$OUT_DIR" ] || mkdir -p "$OUT_DIR"
for out in "$OUT_DIR/$DMG_NAME" "$OUT_DIR/$PKG_NAME"; do
    if [ -e "$out" ] && [ "$FORCE" != "1" ]; then
        die "refusing to overwrite existing output: $out (use --force)"
    fi
done

echo
echo "-- payload identity --"
AU_VER="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' \
            "$AU_SRC/Contents/Info.plist" 2>/dev/null || echo '?')"
AU_BID="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' \
            "$AU_SRC/Contents/Info.plist" 2>/dev/null || echo '?')"
# moduleinfo.json is not strict JSON (JUCE emits trailing commas), so pull the
# first top-level "Version" textually rather than with a JSON parser.
VST3_VER="$(sed -n 's/^[[:space:]]*"Version"[[:space:]]*:[[:space:]]*"\([^"]*\)".*/\1/p' \
            "$VST3_SRC/Contents/Resources/moduleinfo.json" 2>/dev/null | head -1)"
[ -n "$VST3_VER" ] || VST3_VER='?'
echo "AU   version=$AU_VER bundle-id=$AU_BID"
echo "VST3 version=$VST3_VER"
[ "$AU_VER" = "$VERSION" ]   || die "AU version $AU_VER != requested $VERSION (rebuild first)"
[ "$VST3_VER" = "$VERSION" ] || die "VST3 version $VST3_VER != requested $VERSION (rebuild first)"

for bin in "$AU_SRC/Contents/MacOS/ChordEngine" "$VST3_SRC/Contents/MacOS/ChordEngine"; do
    archs="$(lipo -archs "$bin" 2>/dev/null || true)"
    echo "archs: $(basename "$(dirname "$(dirname "$bin")")") -> $archs"
    case "$archs" in
        *x86_64*arm64*|*arm64*x86_64*) ;;
        *) die "not a universal (x86_64+arm64) binary: $bin ($archs)" ;;
    esac
done

echo "-- signing identities --"
# Note: every gate below avoids `... | grep -q` because `set -o pipefail` turns
# grep's early exit into a spurious pipeline failure.
CODESIGN_IDS="$(security find-identity -v -p codesigning)"
ALL_IDS="$(security find-identity -v)"
[[ "$CODESIGN_IDS" == *"$APP_ID"* ]]      || die "codesigning identity not found: $APP_ID"
[[ "$ALL_IDS" == *"$INSTALLER_ID"* ]]     || die "installer identity not found: $INSTALLER_ID"
printf '%s\n' "$ALL_IDS" | grep -F -e "$APP_ID" -e "$INSTALLER_ID" || true
echo "both identities present"

if [ "$DO_NOTARIZE" = "1" ]; then
    xcrun notarytool history --keychain-profile "$NOTARY_PROFILE" >/dev/null 2>&1 \
        || die "notarytool profile '$NOTARY_PROFILE' is not usable (store credentials first)"
    echo "notary profile '$NOTARY_PROFILE' authenticates"
fi

MOUNT_POINT=""
TMP="$(mktemp -d /tmp/chordengine-dmg.XXXXXX)"
trap 'rm -rf "$TMP"' EXIT

# ------------------------------------------------------------------- stage
step "Stage payload (0755 directories)"
PKGROOT="$TMP/pkgroot"
mkdir -p "$PKGROOT/Library/Audio/Plug-Ins/Components" \
         "$PKGROOT/Library/Audio/Plug-Ins/VST3"
chmod 0755 "$PKGROOT" \
           "$PKGROOT/Library" \
           "$PKGROOT/Library/Audio" \
           "$PKGROOT/Library/Audio/Plug-Ins" \
           "$PKGROOT/Library/Audio/Plug-Ins/Components" \
           "$PKGROOT/Library/Audio/Plug-Ins/VST3"

# `ditto` preserves modes, symlinks and xattrs without following anything odd.
ditto "$AU_SRC"   "$PKGROOT/Library/Audio/Plug-Ins/Components/ChordEngine.component"
ditto "$VST3_SRC" "$PKGROOT/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3"

for d in "$PKGROOT" "$PKGROOT/Library" "$PKGROOT/Library/Audio" \
         "$PKGROOT/Library/Audio/Plug-Ins" \
         "$PKGROOT/Library/Audio/Plug-Ins/Components" \
         "$PKGROOT/Library/Audio/Plug-Ins/VST3"; do
    mode="$(stat -f '%Lp' "$d")"
    echo "  $mode  ${d#"$TMP"/}"
    [ "$mode" = "755" ] || die "staging directory is not 0755: $d"
done

# -------------------------------------------------------------------- sign
step "Sign bundles (Developer ID Application, hardened runtime, timestamp)"
SIGNED_BUNDLES=(
    "$PKGROOT/Library/Audio/Plug-Ins/Components/ChordEngine.component"
    "$PKGROOT/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3"
)
for bundle in "${SIGNED_BUNDLES[@]}"; do
    codesign --force --timestamp --options runtime --sign "$APP_ID" "$bundle"
done

# Verify each staged bundle: signature validity, hardened runtime, and that the
# Authority/TeamIdentifier really is the Developer ID identity (not ad-hoc).
for bundle in "${SIGNED_BUNDLES[@]}"; do
    codesign --verify --deep --strict --verbose=2 "$bundle" 2>&1 \
        || die "signature invalid after signing: $bundle"
    info="$(codesign -dv --verbose=4 "$bundle" 2>&1 || true)"
    # codesign -dv prints the flag word inside the CodeDirectory line, e.g.
    # "CodeDirectory v=20400 size=... flags=0x10000(runtime) hashes=...".
    flags="?"
    [[ "$info" =~ flags=([^[:space:]]+) ]] && flags="${BASH_REMATCH[1]}"
    echo "  signed: $(basename "$bundle")  flags=$flags"
    case "$flags" in
        *runtime*) ;;
        *) die "hardened runtime flag missing on $bundle (needed for notarization)" ;;
    esac
    [[ "$info" == *"Authority=$APP_ID"* ]] \
        || die "bundle is not signed by the Developer ID Application identity: $bundle"
    [[ "$info" == *"TeamIdentifier=3A4R5EKM7V"* ]] \
        || die "unexpected TeamIdentifier on $bundle"
done
echo "  both bundles: Developer ID Application signature + hardened runtime verified"

# Verify the signed bundles still depend only on Apple libraries, so hardened
# runtime library validation cannot block loading in a host.
for bin in "$PKGROOT/Library/Audio/Plug-Ins/Components/ChordEngine.component/Contents/MacOS/ChordEngine" \
           "$PKGROOT/Library/Audio/Plug-Ins/VST3/ChordEngine.vst3/Contents/MacOS/ChordEngine"; do
    # otool -L prints a non-indented header per architecture; only the
    # indented lines are real load-command dependencies.
    bad="$(otool -L "$bin" | grep -E '^[[:space:]]' | sed 's/^[[:space:]]*//' \
            | grep -v '^/System/' | grep -v '^/usr/lib/' || true)"
    [ -z "$bad" ] || die "non-system dependencies would break library validation: $bad"
done
echo "  dependencies are Apple-only (library validation safe)"

# ----------------------------------------------------------------- pkgbuild
step "pkgbuild"
UNSIGNED_PKG="$TMP/unsigned.pkg"
pkgbuild --root "$PKGROOT" \
         --identifier "$PKG_IDENTIFIER" \
         --version "$VERSION" \
         --install-location "/" \
         --ownership recommended \
         "$UNSIGNED_PKG"

step "BOM gate (modes / ownership / structure)"
CHECK="$TMP/check"
pkgutil --expand-full "$UNSIGNED_PKG" "$CHECK" >/dev/null
BOM_LINES="$(lsbom -p 'fmuG' "$CHECK/Bom")"
BOM_TAB=$'\t'

expect_line() {
    printf '%s\n' "$BOM_LINES" | grep -F -x -- "$1" \
        || die "BOM assertion failed, expected exactly [$1]"
}
expect_line ".${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/Components${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/VST3${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/Components/ChordEngine.component${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"
expect_line "./Library/Audio/Plug-Ins/VST3/ChordEngine.vst3${BOM_TAB}40755${BOM_TAB}0${BOM_TAB}wheel"

BAD_MODE="$(printf '%s\n' "$BOM_LINES" | \
            grep -E '(^|[[:space:]])[0-9]*700($|[[:space:]])' || true)"
if [ -n "$BAD_MODE" ]; then
    printf '%s\n' "$BAD_MODE" >&2
    die "BOM still contains mode 0700 entries"
fi

PKGINFO_LINE="$(grep -m1 '<pkg-info ' "$CHECK/PackageInfo")" || die "PackageInfo has no <pkg-info>"
PKGINFO_ID="$(printf '%s\n' "$PKGINFO_LINE" | sed -n 's/.* identifier="\([^"]*\)".*/\1/p')"
PKGINFO_VER="$(printf '%s\n' "$PKGINFO_LINE" | sed -n 's/.* version="\([^"]*\)".*/\1/p')"
[ "$PKGINFO_ID" = "$PKG_IDENTIFIER" ] || die "PackageInfo identifier mismatch: $PKGINFO_ID"
[ "$PKGINFO_VER" = "$VERSION" ]       || die "PackageInfo version mismatch: $PKGINFO_VER"
echo "  staging parents archived 40755 / uid 0 / gid wheel"
echo "  PackageInfo identifier=$PKGINFO_ID version=$PKGINFO_VER"

step "productsign (Developer ID Installer)"
SIGNED_PKG="$TMP/$PKG_NAME"
productsign --sign "$INSTALLER_ID" "$UNSIGNED_PKG" "$SIGNED_PKG"
PKG_SIG="$(pkgutil --check-signature "$SIGNED_PKG" 2>&1 || true)"
printf '%s\n' "$PKG_SIG"
[[ "$PKG_SIG" == *"signed by a developer certificate issued by Apple"* ]] \
    || die "installer package is not signed by an Apple-issued developer certificate"
echo "  installer package signature OK"

# --------------------------------------------------------------- notarize
if [ "$DO_NOTARIZE" = "1" ]; then
    step "Notarize installer package"
    NOTARY_OUT="$TMP/notary-pkg.log"
    if ! xcrun notarytool submit "$SIGNED_PKG" --keychain-profile "$NOTARY_PROFILE" \
            --wait --output-format json >"$NOTARY_OUT" 2>&1; then
        cat "$NOTARY_OUT" >&2
        die "notarytool submit failed for the installer package"
    fi
    cat "$NOTARY_OUT"
    notary_status "$NOTARY_OUT" "installer package"
    xcrun stapler staple "$SIGNED_PKG"
    xcrun stapler validate "$SIGNED_PKG" || die "stapler validate failed on the installer package"
    echo "  installer package notarized and stapled"
fi

# --------------------------------------------------------------------- DMG
step "Build DMG"
DMGROOT="$TMP/dmgroot"
mkdir -p "$DMGROOT"
cp "$SIGNED_PKG" "$DMGROOT/$PKG_NAME"
if [ "$DO_NOTARIZE" = "1" ]; then
    PACKAGE_STATUS="Signed and notarized by Music-Prod."
else
    PACKAGE_STATUS="Signed by Music-Prod; not notarized (release candidate only)."
fi
cat > "$DMGROOT/README.txt" <<EOF
$PRODUCT $VERSION for macOS

1. Double-click "$PKG_NAME".
2. Follow the macOS Installer steps.
3. Restart or refresh your DAW if necessary so the plugins are discovered.

The installer places both plugin formats:

  Audio Unit (AU)  ->  /Library/Audio/Plug-Ins/Components/$PRODUCT.component
  VST3             ->  /Library/Audio/Plug-Ins/VST3/$PRODUCT.vst3

Universal binary: Apple Silicon (arm64) + Intel (x86_64).
$PACKAGE_STATUS
EOF

UNSIGNED_DMG="$TMP/$DMG_NAME"
hdiutil create -volname "$VOL_NAME" \
               -srcfolder "$DMGROOT" \
               -fs HFS+ \
               -format UDZO \
               -ov \
               "$UNSIGNED_DMG"

step "Sign DMG (Developer ID Application, secure timestamp)"
codesign --force --timestamp --sign "$APP_ID" "$UNSIGNED_DMG"
codesign --verify --verbose=2 "$UNSIGNED_DMG" 2>&1 \
    || die "DMG signature verification failed"
DMG_SIG_INFO="$(codesign -dv --verbose=4 "$UNSIGNED_DMG" 2>&1 || true)"
[[ "$DMG_SIG_INFO" == *"Authority=$APP_ID"* ]] \
    || die "DMG is not signed by the Developer ID Application identity"
[[ "$DMG_SIG_INFO" == *"TeamIdentifier=3A4R5EKM7V"* ]] \
    || die "unexpected TeamIdentifier on DMG"
echo "  DMG Developer ID Application signature verified"

if [ "$DO_NOTARIZE" = "1" ]; then
    step "Notarize DMG"
    NOTARY_OUT="$TMP/notary-dmg.log"
    if ! xcrun notarytool submit "$UNSIGNED_DMG" --keychain-profile "$NOTARY_PROFILE" \
            --wait --output-format json >"$NOTARY_OUT" 2>&1; then
        cat "$NOTARY_OUT" >&2
        die "notarytool submit failed for the DMG"
    fi
    cat "$NOTARY_OUT"
    notary_status "$NOTARY_OUT" "DMG"
    xcrun stapler staple "$UNSIGNED_DMG"
    xcrun stapler validate "$UNSIGNED_DMG" || die "stapler validate failed on the DMG"
    spctl --assess --type open --context context:primary-signature --verbose=4 "$UNSIGNED_DMG" 2>&1 \
        | tee "$TMP/spctl-dmg.log" || true
    grep -q "source=Notarized Developer ID" "$TMP/spctl-dmg.log" \
        || die "Gatekeeper does not accept the DMG as Notarized Developer ID"
    echo "  DMG notarized, stapled, and Gatekeeper accepted"
fi

# ------------------------------------------------------------ publish+verify
step "Publish to $OUT_DIR"
cp "$SIGNED_PKG" "$OUT_DIR/$PKG_NAME"
cp "$UNSIGNED_DMG" "$OUT_DIR/$DMG_NAME"

step "Verify the published DMG by mounting it read-only"
if [ "$DO_NOTARIZE" = "1" ]; then
    xcrun stapler validate "$OUT_DIR/$DMG_NAME" || die "published DMG stapled ticket validation failed"
fi
MOUNT_POINT="$(hdiutil attach "$OUT_DIR/$DMG_NAME" -nobrowse -readonly 2>/dev/null \
                | tail -1 | sed -n 's/.*\(\/Volumes\/.*\)/\1/p' || true)"
[ -n "$MOUNT_POINT" ] || die "could not determine mount point"
trap 'hdiutil detach "$MOUNT_POINT" >/dev/null 2>&1 || true; rm -rf "$TMP"' EXIT

ls -la "$MOUNT_POINT"
[ -f "$MOUNT_POINT/$PKG_NAME" ] || die "DMG does not contain $PKG_NAME"
[ -f "$MOUNT_POINT/README.txt" ] || die "DMG does not contain README.txt"

MOUNTED_PKG="$MOUNT_POINT/$PKG_NAME"
mounted_sha="$(sha256of "$MOUNTED_PKG")"
published_sha="$(sha256of "$OUT_DIR/$PKG_NAME")"
[ "$mounted_sha" = "$published_sha" ] || die "DMG payload pkg does not match the published pkg"
echo "  payload sha256 identical: $mounted_sha"

if [ "$DO_NOTARIZE" = "1" ]; then
    xcrun stapler validate "$MOUNTED_PKG" || die "stapled ticket missing inside the DMG"
    # spctl exits non-zero when it rejects, so swallow the status and gate on
    # the recorded output instead of the exit code.
    spctl --assess --type install --verbose=4 "$MOUNTED_PKG" 2>&1 \
        | tee "$TMP/spctl.log" || true
    grep -q "source=Notarized Developer ID" "$TMP/spctl.log" \
        || die "Gatekeeper does not accept the installer as Notarized Developer ID"
    echo "  Gatekeeper: accepted (Notarized Developer ID)"
fi

PAYLOAD="$TMP/payload-check"
pkgutil --expand-full "$MOUNTED_PKG" "$PAYLOAD" >/dev/null
for rel in "Library/Audio/Plug-Ins/Components/ChordEngine.component/Contents/MacOS/ChordEngine" \
           "Library/Audio/Plug-Ins/VST3/ChordEngine.vst3/Contents/MacOS/ChordEngine"; do
    [ -f "$PAYLOAD/Payload/$rel" ] || die "payload missing: $rel"
    archs="$(lipo -archs "$PAYLOAD/Payload/$rel")"
    case "$archs" in
        *x86_64*arm64*|*arm64*x86_64*) ;;
        *) die "payload not universal: $rel ($archs)" ;;
    esac
done
echo "  payload contains both universal plugin formats"

# ------------------------------------------------------------------ report
step "Done"
echo "artifact : $OUT_DIR/$DMG_NAME"
echo "size     : $(stat -f '%z' "$OUT_DIR/$DMG_NAME") bytes"
echo "sha256   : $(sha256of "$OUT_DIR/$DMG_NAME")"
echo "installer: $OUT_DIR/$PKG_NAME"
echo "sha256   : $(sha256of "$OUT_DIR/$PKG_NAME")"
echo
if [ "$DO_NOTARIZE" = "1" ]; then
    echo "SIGNED + NOTARIZED DMG OK"
else
    echo "SIGNED DMG OK (NOT NOTARIZED - release candidate only)"
fi
