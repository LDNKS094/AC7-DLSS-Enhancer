#pragma once
#include <windows.h>
#include <filesystem>
#include <stdexcept>
namespace ac7 {
// A request left by an earlier run must never arm during the next game's startup.
inline bool freshCaptureRequest(const std::filesystem::path& path) {
    WIN32_FILE_ATTRIBUTE_DATA attributes{};
    if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &attributes)) {
        const DWORD error = GetLastError();
        if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) return false;
        throw std::runtime_error("Cannot inspect capture request");
    }
    if (attributes.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) throw std::runtime_error("Capture request is a directory");
    FILETIME creation{}, exit{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user)) throw std::runtime_error("Cannot verify capture request against process creation");
    if (CompareFileTime(&attributes.ftLastWriteTime, &creation) <= 0) {
        const auto archive = path.wstring() + L".stale-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64());
        std::filesystem::rename(path, archive);
        return false;
    }
    return true;
}
}
