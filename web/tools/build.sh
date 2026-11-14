#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Build the p32firmware.com server: generate the site and embed it in the Go binary.
#   web/tools/build.sh
#   RFOUNDRY_SITE_URL=https://p32firmware.com/ web/tools/build.sh
set -eu
cd "$(dirname "$0")/../.."
SITE_URL=${RFOUNDRY_SITE_URL:-https://p32firmware.com/}
SITE_NAME=${RFOUNDRY_SITE_NAME:-P32Firmware}
SITE_BRAND_HTML=${RFOUNDRY_SITE_BRAND_HTML:-'P32<b>Firmware</b>'}
mkdir -p build/dist
rm -rf web/site
# The caller supplies a Python environment with site/requirements.txt installed.
PYTHON=${RFOUNDRY_PYTHON:-python3}
if ! "$PYTHON" -c 'import markdown_it, resvg_py' >/dev/null 2>&1; then
	echo "web/tools/build.sh: install site/requirements.txt, or set RFOUNDRY_PYTHON to an environment that has them" >&2
	exit 2
fi
RFOUNDRY_SITE_URL="$SITE_URL" RFOUNDRY_SITE_NAME="$SITE_NAME" RFOUNDRY_SITE_BRAND_HTML="$SITE_BRAND_HTML" \
	RFOUNDRY_NO_CODEPLUG=${RFOUNDRY_NO_CODEPLUG:-0} RFOUNDRY_ONLY_BOARD=${RFOUNDRY_ONLY_BOARD:-dm32} \
	RFOUNDRY_COMMUNITY_URL=${RFOUNDRY_COMMUNITY_URL:-https://github.com/SarahRoseLives/P32} \
	"$PYTHON" site/build.py --out web/site --firmware build/dist \
	--repo-url https://github.com/SarahRoseLives/P32 --ref main
cd web
mkdir -p build
CGO_ENABLED=0 GOOS=${GOOS:-linux} GOARCH=${GOARCH:-amd64} \
	go build -trimpath -ldflags '-s -w' -o build/p32firmware-web .
ls -lh build/p32firmware-web
