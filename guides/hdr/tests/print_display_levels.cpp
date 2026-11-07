// Prints what Windows reports for the primary monitor, to check src/windows_display_levels.hpp on your own machine.
// SPDX-License-Identifier: MIT
#include "windows_display_levels.hpp"

#include <cstdio>

int main() {
    const POINT origin{0, 0};
    const HMONITOR monitor = MonitorFromPoint(origin, MONITOR_DEFAULTTOPRIMARY);
    std::printf("peak %.0f nits, SDR white %.0f nits (0: could not tell)\n", hdr_guide::display_peak_nits(monitor),
                hdr_guide::display_sdr_white_nits(monitor));
    return 0;
}
