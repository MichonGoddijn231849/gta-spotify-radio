#include "spotify_radio.h"

#include <Windows.h>
#include <main.h>

#include <filesystem>
#include <memory>

namespace {

HMODULE g_module = nullptr;

std::filesystem::path module_directory() {
    wchar_t path[MAX_PATH]{};
    GetModuleFileNameW(g_module, path, MAX_PATH);
    return std::filesystem::path(path).parent_path();
}

void ScriptMain() {
    auto radio = std::make_unique<gsr::SpotifyRadio>(module_directory());
    if (!radio->initialize()) {
        while (true) WAIT(1000);
    }
    while (true) {
        radio->tick();
        WAIT(0);
    }
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_module = module;
        DisableThreadLibraryCalls(module);
        scriptRegister(module, ScriptMain);
    } else if (reason == DLL_PROCESS_DETACH) {
        scriptUnregister(module);
    }
    return TRUE;
}
