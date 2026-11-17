#!/bin/sh
# Removes the HP M1132 driver and any print queue that uses it.
# Run with:  sudo /Library/Printers/foo2xqx/uninstall.sh
[ "$(id -u)" = 0 ] || { echo "Run this with sudo."; exit 1; }

for ppd in /etc/cups/ppd/*.ppd; do
    [ -f "$ppd" ] || continue
    if grep -q "/Library/Printers/foo2xqx/rastertoxqx" "$ppd"; then
	q=$(basename "$ppd" .ppd)
	lpadmin -x "$q" && echo "Removed print queue $q"
    fi
done

rm -f /Library/Printers/PPDs/Contents/Resources/HP-LaserJet_Pro_M1132_MFP-foo2xqx.ppd
rm -rf /Library/Printers/foo2xqx
pkgutil --forget io.github.hp-m1132-macos-driver >/dev/null 2>&1
echo "HP M1132 driver removed."
