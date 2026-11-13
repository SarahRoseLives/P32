#!/bin/sh
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
# Build and deploy p32firmware.com. Needs sshm unlocked by fingerprint.
set -eu
SERVER=${P32_SERVER:-P32Firmware}
cd "$(dirname "$0")/.."
web/tools/build.sh

sshm exec "$SERVER" 'set -e
id -u p32firmware >/dev/null 2>&1 || useradd --system --home /var/lib/p32firmware --shell /usr/sbin/nologin p32firmware
install -d -o p32firmware -g p32firmware -m 700 /var/lib/p32firmware
install -d -m 755 /opt/p32firmware
ufw allow 80/tcp >/dev/null
ufw allow 443/tcp >/dev/null'

sshm cp web/build/p32firmware-web "$SERVER:/opt/p32firmware/p32firmware-web.new"
sshm cp deploy/p32firmware.service "$SERVER:/etc/systemd/system/p32firmware.service"

sshm exec "$SERVER" 'set -e
chmod 755 /opt/p32firmware/p32firmware-web.new
mv -f /opt/p32firmware/p32firmware-web.new /opt/p32firmware/p32firmware-web
systemctl daemon-reload
systemctl enable p32firmware >/dev/null
systemctl restart p32firmware
sleep 2
systemctl --no-pager --full status p32firmware | head -20'

echo "deployed; certificate issuance may take a few seconds on the first start"
