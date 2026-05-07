# Installer assets

Drop the following binary assets into this directory before building release
installers. They are referenced by CPack and Qt IFW configuration.

| File              | Purpose                          | Required size                |
|-------------------|----------------------------------|------------------------------|
| `icon.ico`        | Windows installer + executable   | Multi-resolution: 16, 32, 48, 64, 128, 256 px |
| `icon.icns`       | macOS bundle icon                | Multi-resolution `.icns`     |
| `icon.png`        | Linux desktop icon               | 256x256 px PNG (RGBA)        |
| `ifw-banner.png`  | Qt IFW wizard banner             | 460x70 px PNG                |
| `ifw-watermark.png` | Qt IFW wizard sidebar          | 164x314 px PNG               |

All five files above are committed and ready for use by CPack and Qt IFW.
They are derived from the master logo (`terminalSim.png` at the repo root):

* The crane+container symbol is cropped out of the master image (the wordmark
  is omitted) for `icon.{png,ico,icns}` and the IFW banner, since the wordmark
  is illegible at icon scales.
* The IFW watermark uses the full logo+wordmark, scaled to fit the 164 px
  sidebar width.
