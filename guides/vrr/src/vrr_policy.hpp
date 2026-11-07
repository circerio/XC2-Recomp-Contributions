// Variable refresh rate: which present mode and which frame rate cap, from a few facts about the window and display.
// Reference code for guides/vrr/README.md. No Vulkan and no OS calls, so it can be unit tested: map Present to
// VkPresentModeKHR yourself, and call decide() again whenever one of the facts changes.
// SPDX-License-Identifier: MIT
#pragma once

namespace vrr_guide {

enum class WindowMode { Decorated, Borderless, Exclusive };
enum class Vsync { Off, Standard, LowLatency };
enum class DisplayVrr { Unknown, Yes, No };  // what a vendor query said; "no answer" and "all zeros" are Unknown
enum class Present { Mailbox, Fifo, Immediate };
enum class Reason { InUse, SettingOff, DecoratedWindow, DisplayHasNoVrr, RefreshUnknown };

struct Facts {
    bool setting_on = true;
    WindowMode window = WindowMode::Decorated;
    Vsync vsync = Vsync::LowLatency;
    double refresh_hz = 0;  // the monitor the window is on; 0 if not known
    DisplayVrr display = DisplayVrr::Unknown;
};

struct Decision {
    bool active = false;  // false: behave exactly as if this feature did not exist
    Present present = Present::Mailbox;
    double cap_hz = 0;  // 0: no cap from VRR
    Reason reason = Reason::SettingOff;
};

inline constexpr double kCapBelowRefresh = 3.0;
inline constexpr double kMinRefreshHz = 48.0;  // below this, do not treat the display as a VRR one

inline Decision decide(const Facts& f) {
    Decision d;
    d.present = f.vsync == Vsync::Standard ? Present::Fifo
              : f.vsync == Vsync::Off      ? Present::Immediate
                                           : Present::Mailbox;
    if (!f.setting_on) {
        d.reason = Reason::SettingOff;
        return d;
    }
    if (f.window == WindowMode::Decorated) {
        d.reason = Reason::DecoratedWindow;
        return d;
    }
    if (f.display == DisplayVrr::No) {
        d.reason = Reason::DisplayHasNoVrr;
        return d;
    }
    if (!(f.refresh_hz >= kMinRefreshHz)) {
        d.reason = Reason::RefreshUnknown;
        return d;
    }
    d.active = true;
    d.reason = Reason::InUse;
    d.cap_hz = f.refresh_hz - kCapBelowRefresh;
    // Under the cap the display refreshes when a frame arrives, so FIFO has nothing to wait for and MAILBOX has
    // nothing to gain. Both vsync-on settings therefore use FIFO.
    d.present = f.vsync == Vsync::Off ? Present::Immediate : Present::Fifo;
    return d;
}

// The cap to pace to: the lower of the player's cap and VRR's. 0 means unlimited on either side.
inline double effective_cap_hz(const Decision& d, double player_cap_hz) {
    if (d.cap_hz <= 0) return player_cap_hz;
    if (player_cap_hz <= 0) return d.cap_hz;
    return player_cap_hz < d.cap_hz ? player_cap_hz : d.cap_hz;
}

}  // namespace vrr_guide
