// What Windows says about the monitor a window is on: its peak brightness and the white level it uses for SDR content.
// Reference code for guides/hdr/README.md. Windows only; link dxgi. Both return 0 when they cannot tell. Neither is a per-frame
// call: ask when a swapchain is made.
// SPDX-License-Identifier: MIT
#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <dxgi1_6.h>
#include <windows.h>

#include <cwchar>
#include <vector>

namespace hdr_guide {

// DXGI's MaxLuminance for the output showing this monitor. What it reports can depend on the HDR mode the monitor is in.
inline float display_peak_nits(HMONITOR monitor) {
    IDXGIFactory1* factory = nullptr;
    if (FAILED(CreateDXGIFactory1(__uuidof(IDXGIFactory1), reinterpret_cast<void**>(&factory)))) return 0;
    float nits = 0;
    IDXGIAdapter1* adapter = nullptr;
    for (UINT a = 0; nits == 0 && factory->EnumAdapters1(a, &adapter) == S_OK; a++) {
        IDXGIOutput* output = nullptr;
        for (UINT o = 0; nits == 0 && adapter->EnumOutputs(o, &output) == S_OK; o++) {
            IDXGIOutput6* output6 = nullptr;
            if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput6), reinterpret_cast<void**>(&output6)))) {
                DXGI_OUTPUT_DESC1 desc{};
                if (SUCCEEDED(output6->GetDesc1(&desc)) && desc.Monitor == monitor) nits = desc.MaxLuminance;
                output6->Release();
            }
            output->Release();
        }
        adapter->Release();
    }
    factory->Release();
    return nits;
}

// The white level Windows composes SDR content at ("SDR content brightness" in its settings). 1000 in the API means 80 nits.
inline float display_sdr_white_nits(HMONITOR monitor) {
    MONITORINFOEXW info{};
    info.cbSize = sizeof info;
    if (!GetMonitorInfoW(monitor, &info)) return 0;
    UINT32 path_count = 0, mode_count = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &path_count, &mode_count) != ERROR_SUCCESS) return 0;
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(path_count);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(mode_count);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &path_count, paths.data(), &mode_count, modes.data(), nullptr) != ERROR_SUCCESS)
        return 0;
    for (UINT32 i = 0; i < path_count; i++) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof source;
        source.header.adapterId = paths[i].sourceInfo.adapterId;
        source.header.id = paths[i].sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS) continue;
        if (wcscmp(source.viewGdiDeviceName, info.szDevice) != 0) continue;
        DISPLAYCONFIG_SDR_WHITE_LEVEL white{};
        white.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SDR_WHITE_LEVEL;
        white.header.size = sizeof white;
        white.header.adapterId = paths[i].targetInfo.adapterId;
        white.header.id = paths[i].targetInfo.id;
        if (DisplayConfigGetDeviceInfo(&white.header) == ERROR_SUCCESS) return float(white.SDRWhiteLevel) * 80.0f / 1000.0f;
    }
    return 0;
}

}  // namespace hdr_guide
