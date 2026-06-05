#include "config.hpp"
#include <stdexcept>

namespace cppreference::config
{
    namespace
    {
        constexpr int min_ttl_seconds = 1;
        constexpr int max_ttl_seconds = 86400;
        constexpr size_t min_max_size_mb = 1;
        constexpr size_t max_max_size_mb = 1024;
        constexpr int min_max_retries = 0;
        constexpr int max_max_retries = 10;
        constexpr size_t min_thread_pool_size = 1;
        constexpr size_t max_thread_pool_size = 256;
    } // namespace

    Config::Config(std::string_view config_path)
    {
        configuration::Configuration cfg{std::string{config_path}};
        app_config_.cache = cfg.get<CacheConfig>("cache");
        app_config_.client = cfg.get<ClientConfig>("client");
        app_config_.content = cfg.get<ContentConfig>("content");
        app_config_.server = cfg.get<ServerConfig>("server");

        if (app_config_.cache.default_ttl_seconds < min_ttl_seconds)
        {
            throw std::runtime_error("cache.default_ttl_seconds must be >= " + std::to_string(min_ttl_seconds));
        }
        if (app_config_.cache.default_ttl_seconds > max_ttl_seconds)
        {
            throw std::runtime_error("cache.default_ttl_seconds must be <= " + std::to_string(max_ttl_seconds));
        }
        if (app_config_.cache.max_size_mb < min_max_size_mb || app_config_.cache.max_size_mb > max_max_size_mb)
        {
            throw std::runtime_error(
                "cache.max_size_mb must be between " + std::to_string(min_max_size_mb) + " and " + std::to_string(
                    max_max_size_mb));
        }

        if (app_config_.client.timeout_seconds <= 0)
        {
            throw std::runtime_error("client.timeout_seconds must be > 0");
        }
        if (app_config_.client.rate_limit_retry_delay_ms <= 0)
        {
            throw std::runtime_error("client.rate_limit_retry_delay_ms must be > 0");
        }
        if (app_config_.client.max_retries < min_max_retries || app_config_.client.max_retries > max_max_retries)
        {
            throw std::runtime_error(
                "client.max_retries must be between " + std::to_string(min_max_retries) + " and " + std::to_string(
                    max_max_retries));
        }

        if (app_config_.server.thread_pool_size < min_thread_pool_size)
        {
            throw std::runtime_error("server.thread_pool_size must be >= " + std::to_string(min_thread_pool_size));
        }
        if (app_config_.server.thread_pool_size > max_thread_pool_size)
        {
            throw std::runtime_error("server.thread_pool_size must be <= " + std::to_string(max_thread_pool_size));
        }
        if (app_config_.server.http_port > 0 && app_config_.server.http_port < 1024)
        {
            throw std::runtime_error("server.http_port must be >= 1024 or 0 (disabled)");
        }
    }
} // namespace cppreference::config
