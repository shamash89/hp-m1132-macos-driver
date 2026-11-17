#!/bin/sh
# Builds the installer package from build/rastertoxqx and the PPD.
#
#   scripts/make-pkg.sh                       unsigned package
#   SIGN_ID="Developer ID Installer: Name (TEAMID)" scripts/make-pkg.sh
#                                             signed package
#   SIGN_ID=... NOTARY_PROFILE=profile scripts/make-pkg.sh
#                                             signed, notarised and stapled
#
# NOTARY_PROFILE is a keychain profile created once with
#   xcrun notarytool store-credentials
# No credentials are stored in this repository.
set -e
cd "$(dirname "$0")/.."
VERSION=${VERSION:-1.0.0}
ID=io.github.hp-m1132-macos-driver
B=build
ROOT=$B/pkgroot
OUT=$B/HP-M1132-Driver-$VERSION.pkg
PPD=HP-LaserJet_Pro_M1132_MFP-foo2xqx.ppd

[ -x $B/rastertoxqx ] || { echo "run 'make' first"; exit 1; }

rm -rf "$ROOT" $B/component.pkg "$OUT"
mkdir -p "$ROOT/Library/Printers/foo2xqx" "$ROOT/Library/Printers/PPDs/Contents/Resources"
cp $B/rastertoxqx "$ROOT/Library/Printers/foo2xqx/rastertoxqx"
cp ppd/$PPD "$ROOT/Library/Printers/PPDs/Contents/Resources/$PPD"
cp scripts/uninstall.sh "$ROOT/Library/Printers/foo2xqx/uninstall.sh"
cp LICENSE "$ROOT/Library/Printers/foo2xqx/LICENSE"
chmod 755 "$ROOT/Library/Printers/foo2xqx/rastertoxqx" "$ROOT/Library/Printers/foo2xqx/uninstall.sh"
chmod 644 "$ROOT/Library/Printers/PPDs/Contents/Resources/$PPD" "$ROOT/Library/Printers/foo2xqx/LICENSE"

# With a Developer ID Application identity, sign the filter itself too
# (required for notarisation).
if [ -n "$APP_SIGN_ID" ]; then
    codesign --force --timestamp --options runtime -s "$APP_SIGN_ID" \
	"$ROOT/Library/Printers/foo2xqx/rastertoxqx"
fi

# --ownership recommended: files are installed root:wheel, which CUPS requires
# before it will run a filter.
pkgbuild --root "$ROOT" --identifier $ID --version "$VERSION" \
    --install-location / --ownership recommended $B/component.pkg

cat > $B/distribution.xml <<EOF
<?xml version="1.0" encoding="utf-8"?>
<installer-gui-script minSpecVersion="2">
    <title>HP LaserJet Pro M1132 MFP Driver</title>
    <license file="LICENSE"/>
    <welcome file="welcome.txt"/>
    <options customize="never" require-scripts="false" hostArchitectures="arm64,x86_64"/>
    <volume-check><allowed-os-versions><os-version min="11.0"/></allowed-os-versions></volume-check>
    <choices-outline><line choice="default"/></choices-outline>
    <choice id="default"><pkg-ref id="$ID"/></choice>
    <pkg-ref id="$ID" version="$VERSION">component.pkg</pkg-ref>
</installer-gui-script>
EOF
mkdir -p $B/resources
cp LICENSE $B/resources/LICENSE
cp scripts/welcome.txt $B/resources/welcome.txt

if [ -n "$SIGN_ID" ]; then
    productbuild --distribution $B/distribution.xml --package-path $B \
	--resources $B/resources --sign "$SIGN_ID" --timestamp "$OUT"
else
    productbuild --distribution $B/distribution.xml --package-path $B \
	--resources $B/resources "$OUT"
    echo "NOTE: unsigned package. Gatekeeper will block it on other Macs."
fi

if [ -n "$SIGN_ID" ] && [ -n "$NOTARY_PROFILE" ]; then
    xcrun notarytool submit "$OUT" --keychain-profile "$NOTARY_PROFILE" --wait
    xcrun stapler staple "$OUT"
fi

rm -rf "$ROOT" $B/component.pkg $B/distribution.xml $B/resources
ls -l "$OUT"
shasum -a 256 "$OUT"
