#include "config.hpp"
#include <fstream>
#include <nlohmann/json.hpp>
#include <stdexcept>

namespace cppreference::config {

namespace {

constexpr int min_ttl_seconds = 1;
constexpr int max_ttl_seconds = 86400;
constexpr size_t min_max_size_mb = 1;
constexpr size_t max_max_size_mb = 1024;
constexpr int min_max_retries = 0;
constexpr int max_max_retries = 10;
constexpr size_t min_thread_pool_size = 1;
constexpr size_t max_thread_pool_size = 256;

const nlohmann::json& require_section(const nlohmann::json& json, std::string_view key, std::string_view config_path) {
    if (!json.contains(key)) {
        throw std::runtime_error("Missing required section '" + std::string{key} + "' in config: " + std::string{config_path});
    }
    return json[key];
}

} // namespace

Config::Config(std::string_view config_path) {
    std::ifstream file{std::string{config_path}};
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + std::string{config_path});
    }

    nlohmann::json json_data;
    try {
        json_data = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& exc) { // NOLINTNEXTLINE(bugprone-empty-catch)
        throw std::runtime_error("JSON parse error in '" + std::string{config_path} + "': " + exc.what());
    }

    const auto& cache_section = require_section(json_data, "cache", config_path);
    cache_config_.enabled = cache_section.value("enabled", true);
    cache_config_.path = cache_section.value("path", "./cache.db");
    cache_config_.default_ttl_seconds = cache_section.value("default_ttl_seconds", 300LL);
    cache_config_.max_size_mb = cache_section.value("max_size_mb", 50u);

    if (cache_config_.default_ttl_seconds < min_ttl_seconds) {
        throw std::runtime_error("cache.default_ttl_seconds must be >= " + std::to_string(min_ttl_seconds));
    }
    if (cache_config_.default_ttl_seconds > max_ttl_seconds) {
        throw std::runtime_error("cache.default_ttl_seconds must be <= " + std::to_string(max_ttl_seconds));
    }
    if (cache_config_.max_size_mb < min_max_size_mb || cache_config_.max_size_mb > max_max_size_mb) {
        throw std::runtime_error("cache.max_size_mb must be between " + std::to_string(min_max_size_mb) + " and " + std::to_string(max_max_size_mb));
    }

    const auto& client_section = require_section(json_data, "client", config_path);
    client_config_.base_url = client_section.value("base_url", "https://en.cppreference.com");
    client_config_.timeout_seconds = client_section.value("timeout_seconds", 10);
    client_config_.rate_limit_retry_delay_ms = client_section.value("rate_limit_retry_delay_ms", 1000);
    client_config_.max_retries = client_section.value("max_retries", 3);

    if (client_config_.timeout_seconds <= 0) {
        throw std::runtime_error("client.timeout_seconds must be > 0");
    }
    if (client_config_.rate_limit_retry_delay_ms <= 0) {
        throw std::runtime_error("client.rate_limit_retry_delay_ms must be > 0");
    }
    if (client_config_.max_retries < min_max_retries || client_config_.max_retries > max_max_retries) {
        throw std::runtime_error("client.max_retries must be between " + std::to_string(min_max_retries) + " and " + std::to_string(max_max_retries));
    }

    const auto& content_section = require_section(json_data, "content", config_path);
    content_config_.max_output_chars = content_section.value("max_output_chars", 8000u);

    if (content_config_.max_output_chars == 0) {
        throw std::runtime_error("content.max_output_chars must be > 0");
    }

    const auto& server_section = require_section(json_data, "server", config_path);
    server_config_.thread_pool_size = server_section.value("thread_pool_size", 10u);
    server_config_.http_port = server_section.value("http_port", 0u);

    if (server_config_.thread_pool_size < min_thread_pool_size) {
        throw std::runtime_error("server.thread_pool_size must be >= " + std::to_string(min_thread_pool_size));
    }
    if (server_config_.thread_pool_size > max_thread_pool_size) {
        throw std::runtime_error("server.thread_pool_size must be <= " + std::to_string(max_thread_pool_size));
    }
    if (server_config_.http_port > 0 && server_config_.http_port < 1024) {
        throw std::runtime_error("server.http_port must be >= 1024 or 0 (disabled)");
    }
}

} // namespace cppreference::config
