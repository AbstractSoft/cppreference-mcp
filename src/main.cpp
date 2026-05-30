#include "server/server.hpp"
#include "config/config.hpp"
#include <filesystem>
#include <iostream>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

namespace {

std::string get_exe_dir() {
#if defined(__APPLE__)
    char path[PATH_MAX];
    uint32_t size = sizeof(path);
    if (_NSGetExecutablePath(path, &size) == 0) {
        return std::filesystem::path{path}.parent_path().string();
    }
#elif defined(__linux__)
    char path[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);
    if (len != -1) {
        path[len] = '\0';
        return std::filesystem::path{path}.parent_path().string();
    }
#endif
    return {};
}

} // namespace

int main() {
    std::string config_path = "config.json";
    
    auto exe_dir = get_exe_dir();
    if (!exe_dir.empty()) {
        auto fallback_path = exe_dir + "/config.json";
        if (std::filesystem::exists(fallback_path)) {
            config_path = fallback_path;
        }
    }
    
    try {
        if (std::filesystem::exists(config_path)) {
            cppreference::config::Config cfg{config_path};
            cppreference::server::Server server{cfg.get_client_config()};
            server.run();
        } else {
            cppreference::config::ClientConfig default_config;
            default_config.base_url = "https://en.cppreference.com";
            default_config.timeout_seconds = 10;
            cppreference::server::Server server{default_config};
            server.run();
        }
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
