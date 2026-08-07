Populated by the `release` job in `.github/workflows/build.yaml` on every
`v*` tag push, with the *factory* image per env — same file
`dist/esp32-hwmonitor-v<ver>-pio-<env>.bin` that job already attaches to the
GitHub release, just renamed and copied here.

`platformio` requires both a PR *and* a verified commit signature. The job
can't satisfy the second part on its own — `GITHUB_TOKEN` has no GPG key
registered with GitHub, and neither the Git Data API nor a squash-merge
sidesteps `required_signatures` (tried both, see PRs #47/#48 in this repo's
history). So the job opens a `docs/firmware-v<ver>` PR against `platformio`
with an **unsigned** commit that GitHub will refuse to merge as-is. To
actually publish an update, someone with push access and their own GPG key
needs to:

```
git fetch origin docs/firmware-v<ver>
git checkout docs/firmware-v<ver>
git commit --amend -S --no-edit
git push --force-with-lease
```

...then merge the PR normally. (Only needed for the *docs* commit — the
GitHub release itself, with all `.bin` assets, is already published by the
time this PR shows up, no signature issue there.) If this manual step gets
annoying, the real fix is provisioning a bot GPG key for the workflow
(generate a key, add its public half to an account with write access under
Settings → SSH and GPG keys, store the private key + passphrase as repo
secrets, `git config commit.gpgsign` in the job) — not implemented here to
avoid taking that step without asking first.

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
