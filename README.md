# HP LaserJet Pro M1132 MFP driver for macOS

An open-source USB printer driver for the HP LaserJet Pro M1132 MFP on current
macOS, where HP no longer provides one. It works on Apple silicon and Intel
Macs and needs no VM, no Ghostscript, and no changes to SIP or other security
settings.

Not made or supported by HP. No warranty.

## Status

| | |
|---|---|
| Tested | HP LaserJet Professional M1132 MFP over USB, macOS 27.2.1, Apple silicon |
| Untested | Intel Macs, macOS 11 to 26, other XQX printers |
| Supports | Black-and-white printing at 600 dpi, single-sided |
| Does not support | Scanning, automatic duplex |

When a print queue is created, CUPS warns that "printer drivers are deprecated".
PPD-based drivers like this one still work today, but a future macOS may remove
them.

## Install

1. Download the `.pkg` from the Releases page and open it.
2. Connect the printer by USB and switch it on.
3. Open **System Settings > Printers & Scanners > Add Printer** and select the
   printer. If the driver is not chosen automatically, pick
   **HP LaserJet Pro M1132 MFP (foo2xqx, rastertoxqx)** under "Select Software".

Release packages are signed with a Developer ID and notarised by Apple, so they
open with a normal double-click. A package you build yourself without signing
will be blocked on the first try; go to **System Settings > Privacy & Security**
and click **Open Anyway**.

Or from Terminal, with the printer connected:

```bash
lpadmin -p HP_LaserJet_M1132 -E -v "$(lpinfo -v | awk '$2 ~ /^usb:.*M1132/ {print $2; exit}')" -P /Library/Printers/PPDs/Contents/Resources/HP-LaserJet_Pro_M1132_MFP-foo2xqx.ppd
```

## Check the scale

Print `test/ruler-letter.pdf` or `test/ruler-a4.pdf` at 100% (untick "Scale to
fit") and measure the rulers and the 100 mm square with a real ruler.

## Uninstall

```bash
sudo /Library/Printers/foo2xqx/uninstall.sh
```

This removes the filter, the PPD and any print queue using them.

## Print options

- Paper: Letter, Legal, Executive, A4, A5, B5, envelopes #10 / DL / C5 / Monarch
- Print Density 1 to 5 (default 3)
- Toner Save on or off

## Build from source

Needs Xcode or the Xcode command-line tools.

```bash
make            # universal filter in build/
make check      # run the test page through the whole chain and decode it
make pkg        # unsigned installer in build/
```

Signed and notarised package (needs an Apple Developer ID):

```bash
APP_SIGN_ID="Developer ID Application: NAME (TEAMID)" SIGN_ID="Developer ID Installer: NAME (TEAMID)" NOTARY_PROFILE=profile-name make pkg
```

## How it works

```
app -> PDF -> cgpdftoraster (Apple) -> rastertoxqx (this project) -> CUPS usb backend -> printer
```

- The M1132 speaks HP's XQX stream protocol. The encoder for it is `foo2xqx`
  from Rick Richardson's [foo2zjs](https://tracker.debian.org/pkg/foo2zjs)
  project, which lists this printer as "M1132s".
- `src/rastertoxqx.c` is a small CUPS filter. It reads the 8-bit grey raster
  that macOS produces, places it on a full sheet (Apple's rasteriser renders
  only the printable area), halftones it with a clustered-dot screen of about
  106 lpi, and hands the 1-bit page to foo2xqx.
- foo2xqx is compiled into the filter and runs in a forked child, so the
  filter never executes another program. That keeps it inside what the CUPS
  sandbox allows.
- Files install to `/Library/Printers`, the standard location for third-party
  printer drivers.

## Source layout

| Path | Contents |
|---|---|
| `src/rastertoxqx.c` | The CUPS filter |
| `ppd/` | Printer description file |
| `vendor/foo2zjs/` | Unmodified files from foo2zjs 20200505 (Debian `20200505dfsg0`): `foo2xqx.c`, `xqxdecode.c`, JBIG-KIT |
| `scripts/` | Package build, installer text, uninstaller |
| `test/` | 100%-scale ruler page |

## Licence

GNU General Public License, version 2. See `LICENSE`, and
`vendor/foo2zjs/COPYING` for the upstream notices. foo2zjs is by Rick
Richardson; JBIG-KIT is by Markus Kuhn.
