<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# P25 UI lab

HTML-only light-mode mockups for the DM-32UV P25 build. Nothing here is linked into the firmware, flashes a radio or transmits.

## Open

From the repository root:

```
python3 -m http.server 8000
```

Then open <http://localhost:8000/mockups/p25-ui/>. Opening `index.html` directly also works; the boot screen uses
`../../boards/dm32/boot_splash.jpg`.

## Constraints baked in

- 240 × 320 display, 16-color 4bpp palette and the board's 10 px side / 5 px top-bottom safe area.
- PTT, SK1/SK2, 3 × 4 keypad and rotary encoder. No touchscreen.
- This P25 image: FM and P25 Phase 1; DMR is off.
- Conventional P25 defaults: 446.0000 MHz, NAC 293, TG 1, UID 2. Trunking remains a Tools path.
- Long `*` keeps the key-lock meaning; short `*` is the FM/P25 switch in this build.

## Interaction

`Boot`, `FM home`, `P25 home`, `P25 trunked`, `P25 RX`, `P25 TX`, `Quick`, `P25 setup` and `Tools` switch the mock
screen. The two controls under the device simulate a short `*` and a held PTT; the device screen itself is not a touch
target. Space holds PTT; `*` toggles FM/P25.
