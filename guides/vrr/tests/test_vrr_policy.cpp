// Every row of the table in guides/vrr/README.md section 2.
// SPDX-License-Identifier: MIT
#include "vrr_policy.hpp"

#include <cstdio>
#include <initializer_list>

using namespace vrr_guide;

static int failures = 0;

#define CHECK(cond)                                                     \
    do {                                                                \
        if (!(cond)) {                                                  \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);         \
            failures++;                                                 \
        }                                                               \
    } while (0)

static Facts fullscreen(WindowMode w, Vsync v, double hz = 240) {
    Facts f;
    f.window = w;
    f.vsync = v;
    f.refresh_hz = hz;
    return f;
}

int main() {
    for (WindowMode w : {WindowMode::Borderless, WindowMode::Exclusive}) {
        Decision low = decide(fullscreen(w, Vsync::LowLatency));
        CHECK(low.active && low.present == Present::Fifo && low.cap_hz == 237 && low.reason == Reason::InUse);
        Decision std_ = decide(fullscreen(w, Vsync::Standard));
        CHECK(std_.active && std_.present == Present::Fifo && std_.cap_hz == 237);
        Decision off = decide(fullscreen(w, Vsync::Off));
        CHECK(off.active && off.present == Present::Immediate && off.cap_hz == 237);
    }
    CHECK(decide(fullscreen(WindowMode::Exclusive, Vsync::Standard, 144)).cap_hz == 141);

    // An ordinary decorated window keeps the plain mapping and no cap, whatever the setting says.
    for (bool on : {true, false}) {
        Facts f = fullscreen(WindowMode::Decorated, Vsync::LowLatency);
        f.setting_on = on;
        Decision d = decide(f);
        CHECK(!d.active && d.present == Present::Mailbox && d.cap_hz == 0);
        CHECK(d.reason == (on ? Reason::DecoratedWindow : Reason::SettingOff));
    }

    // Setting off: the plain mapping, fullscreen or not.
    {
        Facts f = fullscreen(WindowMode::Borderless, Vsync::Standard);
        f.setting_on = false;
        Decision d = decide(f);
        CHECK(!d.active && d.present == Present::Fifo && d.cap_hz == 0 && d.reason == Reason::SettingOff);
        f.vsync = Vsync::Off;
        CHECK(decide(f).present == Present::Immediate);
    }

    // A vendor query that gave no answer is not a "no": the rules still apply. Only an explicit "no" turns them off.
    {
        Facts f = fullscreen(WindowMode::Borderless, Vsync::LowLatency);
        f.display = DisplayVrr::Unknown;
        CHECK(decide(f).active);
        f.display = DisplayVrr::Yes;
        CHECK(decide(f).active);
        f.display = DisplayVrr::No;
        Decision d = decide(f);
        CHECK(!d.active && d.reason == Reason::DisplayHasNoVrr && d.present == Present::Mailbox);
    }

    // The refresh rate has to be known and at least the minimum.
    CHECK(decide(fullscreen(WindowMode::Borderless, Vsync::LowLatency, 0)).reason == Reason::RefreshUnknown);
    CHECK(decide(fullscreen(WindowMode::Borderless, Vsync::LowLatency, 47.9)).reason == Reason::RefreshUnknown);
    CHECK(decide(fullscreen(WindowMode::Borderless, Vsync::LowLatency, 48)).active);

    // The lower cap wins; 0 means unlimited on either side.
    {
        Decision on = decide(fullscreen(WindowMode::Borderless, Vsync::LowLatency));
        CHECK(effective_cap_hz(on, 60) == 60);
        CHECK(effective_cap_hz(on, 0) == 237);
        CHECK(effective_cap_hz(on, 360) == 237);
        Decision off = decide(fullscreen(WindowMode::Decorated, Vsync::LowLatency));
        CHECK(effective_cap_hz(off, 60) == 60);
        CHECK(effective_cap_hz(off, 0) == 0);
    }

    std::printf(failures ? "%d FAILED\n" : "vrr_policy: all checks passed\n", failures);
    return failures ? 1 : 0;
}
