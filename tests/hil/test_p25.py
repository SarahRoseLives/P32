# SPDX-License-Identifier: MIT
# Copyright (c) 2026 The RadioFoundry Authors
"""The P25 control-channel monitor's screen (docs/spec/p25.md 8) over the debug link.

Tools > P25 takes the receiver for the monitor: the p25 view section says on=1 and the screen is "p25". Leaving with
BACK gives the receiver back (on=0). A control channel on the air fills the view (valid=1, a nonzero NAC); without one
the screen stays on "listening" and the test still checks the entry and the exit. The decode itself is checked on the
bench against a known transmitter (p25.md 7.2), not here.

The P25 tool is the last entry of Tools (its number is past the digit keys), so the test focuses the last row with the
UP key rather than a number.
"""
import time

import pytest

from rfoundry_hil import PRESS, skip


def monitor(radio):
    return radio.view(["p25"])


@pytest.mark.feature("p25")
def test_p25_monitor_screen(radio):
    radio.ensure_awake()
    v = monitor(radio)
    if "p25.on" not in v:
        skip("not_built", "the firmware has no P25 monitor view (CONFIG_P25 off)")
    link = radio.view(["link"])
    if link.get("ui.locked") == "1":
        radio.press("quick", PRESS)
        radio.wait_for(lambda x: x.get("ui.locked") == "0", sections=("link",), what="keys unlocked")
    if radio.screen() != "home":
        radio.press("back", PRESS)
        radio.wait_for(lambda x: x.get("ui.screen") == "home", sections=("link",), what="screen home")
    if radio.core().get("vfo") == "1":
        radio.link.vfo(False)
        radio.wait_for(lambda x: x.get("vfo") == "0", what="VFO off")

    # Home > Hub > Tools, then UP to the last row (the P25 monitor; its number is past the digit keys)
    radio.press("select")
    radio.wait_for(lambda x: x.get("ui.screen") == "hub", sections=("link",), what="screen hub")
    radio.press("6")
    radio.wait_for(lambda x: x.get("ui.screen") == "tools", sections=("link",), what="screen tools")
    radio.press("up")
    radio.press("select")
    radio.wait_for(lambda x: x.get("ui.screen") == "p25", sections=("link",), what="screen p25")
    radio.wait_for(lambda x: x.get("p25.on") == "1", sections=("p25",), what="the monitor listening")

    # A control channel heard: the NAC and DUID come from the NID; the rest fills as TSBKs arrive
    v = monitor(radio)
    if v.get("p25.valid") == "1":
        assert int(v["p25.nac"], 16) != 0, "a decoded control channel has a NAC"

    # BACK leaves the monitor and the receiver goes back to the channel
    radio.press("back")
    radio.wait_for(lambda x: x.get("ui.screen") == "tools", sections=("link",), what="screen tools after Back")
    radio.wait_for(lambda x: x.get("p25.on") == "0", sections=("p25",), what="the monitor stopped")
