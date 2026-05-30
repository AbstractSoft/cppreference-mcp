#include "config.hpp"
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace cppreference::config {

namespace {

const nlohmann::json& require_section(const nlohmann::json& j, const std::string& key, const std::string& config_path) {
    if (!j.contains(key)) {
        throw std::runtime_error("Missing required section '" + key + "' in config: " + config_path);
    }
    return j[key];
}

} // namespace

Config::Config(const std::string& config_path) {
    std::ifstream file{config_path};
    if (!file.is_open()) {
        throw std::runtime_error("Cannot open config file: " + config_path);
    }

    nlohmann::json j;
    try {
        j = nlohmann::json::parse(file);
    } catch (const nlohmann::json::parse_error& e) {
        throw std::runtime_error("JSON parse error in '" + config_path + "': " + e.what());
    }

    const auto& cache_section = require_section(j, "cache", config_path);
    cache_config_.enabled = cache_section.value("enabled", true);
    cache_config_.path = cache_section.value("path", "./cache.db");
    cache_config_.default_ttl_seconds = cache_section.value("default_ttl_seconds", 300LL);
    cache_config_.max_size_mb = cache_section.value("max_size_mb", 50u);

    if (cache_config_.default_ttl_seconds <= 0) {
        throw std::runtime_error("cache.default_ttl_seconds must be > 0");
    }
    if (cache_config_.max_size_mb == 0 || cache_config_.max_size_mb > 1024) {
        throw std::runtime_error("cache.max_size_mb must be between 1 and 1024");
    }

    const auto& client_section = require_section(j, "client", config_path);
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
    if (client_config_.max_retries < 0 || client_config_.max_retries > 10) {
        throw std::runtime_error("client.max_retries must be between 0 and 10");
    }

    const auto& content_section = require_section(j, "content", config_path);
    content_config_.max_output_chars = content_section.value("max_output_chars", 8000u);

    if (content_config_.max_output_chars == 0) {
        throw std::runtime_error("content.max_output_chars must be > 0");
    }

    const auto& server_section = require_section(j, "server", config_path);
    server_config_.thread_pool_size = server_section.value("thread_pool_size", 10u);

    if (server_config_.thread_pool_size == 0) {
        throw std::runtime_error("server.thread_pool_size must be > 0");
    }
    if (server_config_.thread_pool_size > 256) {
        throw std::runtime_error("server.thread_pool_size must be <= 256");
    }
}

} // namespace cppreference::config
