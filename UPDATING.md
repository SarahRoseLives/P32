<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# Updating p32firmware.com and its firmware

Run these commands from this directory. The site is generated from `site/` and `build/dist/`, embedded in the Go
server, and served by `p32firmware.service` on the `P32Firmware` sshm server. A deploy does not push to GitHub.

## What is updated

| Change | Source | Result |
|---|---|---|
| Page, CSS, JavaScript, translations | `site/` | `web/site/`, then the embedded site in `web/build/p32firmware-web` |
| Firmware offered by the flasher | Release image and manifest in `build/dist/` | `web/site/firmware/dm32/` and `web/site/data/releases.json` |
| Server behavior (TLS, redirects, cache headers, reports) | `web/main.go` | `web/build/p32firmware-web` |
| Deployment and service | `deploy/` | Files on the `P32Firmware` server |

Never put a codec image, stock firmware, key, dump or recording in `build/dist/` or in the repository. The flasher
release is the codec-less application image; the radio keeps or supplies its own codec.

## One-time local requirements

```sh
python3 -m venv "$HOME/.venvs/p32firmware"
"$HOME/.venvs/p32firmware/bin/pip" install -r site/requirements.txt

# The DM32 firmware is C-SKY. This must print a compiler path:
command -v csky-elf-gcc || ls "$HOME/toolchains/csky-elf/bin/csky-elf-gcc"

# Go 1.24 or newer, Node 20 or newer, git and make.
```

The local Python environment is passed to every site build:

```sh
export RFOUNDRY_PYTHON="$HOME/.venvs/p32firmware/bin/python"
```

`deploy/deploy.sh` uses sshm and needs the `P32Firmware` server unlocked by fingerprint.

## Update the firmware

1. Choose the release name. The current release is `v26.11`; the next one in the same month is `v26.11.1`, and the
   next month is `v26.12`.

   ```sh
   VERSION=v26.11.1
   python3 tools/release_version.py check "$VERSION"
   ```

   If you are replacing the current image without publishing a release, keep `VERSION=v26.11`. A new version name
   should also become a GitHub release tag; the site footer links to `releases/tag/<version>`.

2. Build the DM32 image. This stamps the release name into the image.

   ```sh
   make BOARD=dm32 RELEASE="$VERSION" -j"$(nproc)"
   ls -l build/dm32/rfoundry_dm32.bin
   ```

3. Package it for the flasher. Remove the old images first: the flasher chooses the first matching image in
   `data/releases.json`, not the highest version.

   ```sh
   mkdir -p build/dist
   rm -f build/dist/rfoundry-dm32-*.bin build/dist/rfoundry-dm32-*.json build/dist/SHA256SUMS
   python3 tools/release.py --board dm32 --version "$VERSION" --out build/dist
   ls -l build/dist
   ```

   `build/dist` should contain exactly:

   ```
   rfoundry-dm32-<version>.bin
   rfoundry-dm32-<version>.json
   SHA256SUMS
   ```

   `release.py` refuses an image that contains the codec. Do not bypass that check.

4. Rebuild the site so it embeds the new image and manifest:

   ```sh
   web/tools/build.sh
   ```

   A site build also needs the firmware files. If `build/dist` is empty, the flasher has nothing to install.

## Update only the website

After editing `site/`, `web/main.go`, or `deploy/`:

```sh
export RFOUNDRY_PYTHON="$HOME/.venvs/p32firmware/bin/python"
web/tools/build.sh
```

This regenerates `web/site/` and rebuilds `web/build/p32firmware-web`. Do not edit `web/site/` by hand: it is
generated and replaced on every build.

## Check before deploying

```sh
# Optional but recommended.
make -C tests/host
site/tests/run.sh
```

Preview locally:

```sh
P32_REPORTS=/tmp/p32firmware-reports ./web/build/p32firmware-web -dev :8080
```

Open `http://localhost:8080/install/`. Web Serial works on localhost. Start the flasher, then select the DM-32UV:
the expected steps are:

```
Radio -> Connect -> Back Up and Verify -> Install -> Verify
```

There is no Build step. Check this after selecting the radio, not only on the first page; the radio's backend
(`site/static/js/flasher/c7000ui.js`) supplies the later steps.

Also check:

```sh
ls -l web/site/firmware/dm32/
python3 -m json.tool web/site/data/releases.json
grep -o 'app.js?v=[0-9a-f]*' web/site/install/index.html
```

## Deploy

```sh
export RFOUNDRY_PYTHON="$HOME/.venvs/p32firmware/bin/python"
deploy/deploy.sh
```

`deploy/deploy.sh` runs `web/tools/build.sh` again, copies the binary and service file to the server, restarts
`p32firmware.service`, and leaves TLS issuance and renewal to the service.

Then verify the live site:

```sh
curl -sSI https://p32firmware.com/healthz
curl -sS https://p32firmware.com/install/ | grep -o 'app.js?v=[0-9a-f]*'
curl -sS https://p32firmware.com/data/releases.json | python3 -m json.tool

sshm exec P32Firmware 'systemctl --no-pager --full status p32firmware | head -20'
sshm exec P32Firmware 'journalctl -u p32firmware -n 50 --no-pager'
```

Open `https://p32firmware.com/install/`, start the flasher, and select the DM-32UV. Confirm the same five steps and
that the footer shows the expected firmware version and current build time. `/flash/` redirects to `/install/`.

## Server layout

| Path | What |
|---|---|
| `/opt/p32firmware/p32firmware-web` | The deployed Go server |
| `/etc/systemd/system/p32firmware.service` | systemd unit |
| `/var/lib/p32firmware/autocert/` | Let's Encrypt certificate cache |
| `/var/lib/p32firmware/reports/` | Optional `/v1/` reports and flash logs |
| `p32firmware.com:80,443` | ACME challenge and HTTPS |

The service redirects `www.p32firmware.com` to `p32firmware.com`, serves `/healthz`, and gives `/install/`, `/`,
`/static/js/` and `/static/css/` no-store cache headers.

## Rollback

Keep the previous release files before deleting them if you may need to roll back:

```sh
cp -a build/dist "/tmp/p32-dist-$(date +%Y%m%d-%H%M%S)"
```

To roll back the site, check out the previous source revision, restore the matching `build/dist` files, and run
`deploy/deploy.sh` again. The deploy overwrites the server binary, so the previous good build is not kept on the
server automatically.

## Troubleshooting

| Symptom | Check |
|---|---|
| `site/build.py` cannot import `markdown_it` or `resvg_py` | `RFOUNDRY_PYTHON` points at the venv with `site/requirements.txt` installed |
| `make BOARD=dm32` cannot find a compiler | `csky-elf-gcc` is in `~/toolchains/csky-elf/bin/` or on `PATH` |
| The flasher has no firmware | `build/dist` has a `.bin`, its `.json`, and `SHA256SUMS`; rebuild `web/site` |
| The flasher installs an old image | Remove old `rfoundry-dm32-*` files from `build/dist`, repackage, then rebuild |
| The flasher shows Build | Select the DM-32UV and re-check `c7000ui.js`; its backend supplies `connect`, not `build` |
| A deployment still shows an old asset | Confirm the live `app.js?v=` value and the footer build time; rebuild and redeploy if they do not change |
| TLS does not start on the first deploy | Wait for ACME issuance, then check `journalctl -u p32firmware` and DNS for `p32firmware.com` |
