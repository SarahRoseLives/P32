<!-- SPDX-License-Identifier: MIT -->
<!-- Copyright (c) 2026 The RadioFoundry Authors -->

# Channel Instrument

A second, standalone UI concept for the DM-32UV P25 build. It keeps one instrument frame and changes the object it
operates: a conventional channel, a trunked system's talkgroup, or an analog frequency.

## Open

From the repository root:

```
python3 -m http.server 8000
```

Open <http://localhost:8000/mockups/channel-instrument/>. The other light study remains at
`/mockups/p25-ui/`.

## Screens

- Conventional P25 home, trunked P25 home and analog FM home
- RX and TX in the same frame
- One channel list for all three families
- Local programming: Program, talkgroups, edit conventional, edit trunked, edit system, write review

## Design rules

- 240 × 320, 16 colors, 4 bpp, no touch; PTT, SK1/SK2, keypad and encoder only.
- The instrument never becomes a menu for primary operation; TX and RX replace the same frame.
- Conventional and trunked channels are peers in the channel list, with different technical decks.
- FPP is a local mode: categories -> object -> field -> explicit write review.
- The mock proposes `*` cycling FM / P25 conventional / P25 trunked; the current firmware uses `*` for FM/P25.
- Nothing here is linked into firmware, flashed or transmitted.
