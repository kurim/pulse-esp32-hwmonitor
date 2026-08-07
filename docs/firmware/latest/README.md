Populated by the `release` job in `.github/workflows/build.yaml` on every
`v*` tag push, with the *factory* image per env — same file
`dist/esp32-hwmonitor-v<ver>-pio-<env>.bin` that job already attaches to the
GitHub release, just renamed and copied here (and committed straight back to
the `platformio` branch, since that's what this Pages site is expected to
serve from):

```
esp32-hwmonitor-pio-cyd.bin
esp32-hwmonitor-pio-cyd-v3.bin
esp32-hwmonitor-pio-jc8048w550.bin
esp32-hwmonitor-pio-esp32.bin
esp32-hwmonitor-pio-esp32s3-4mb.bin
esp32-hwmonitor-pio-esp32s3-8mb.bin
esp32-hwmonitor-pio-esp32c3.bin
VERSION            <- plain text, e.g. "0.4.9"
```

Binaries live here instead of being fetched from the GitHub release directly
because `release-assets.githubusercontent.com` doesn't send
`Access-Control-Allow-Origin`, so a cross-origin `fetch()` from the Pages
site fails before esp-web-tools ever gets bytes to flash — same-origin is the
only way this works from a browser. See `../../flasher.js`.
