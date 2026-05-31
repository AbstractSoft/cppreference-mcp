#ifndef CPPREFERENCE_CONFIG_HPP
#define CPPREFERENCE_CONFIG_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace cppreference::config {

struct CacheConfig {
    bool enabled = true;
    std::string path = "./cache.db";
    int64_t default_ttl_seconds = 300;
    size_t max_size_mb = 50;
};

struct ClientConfig {
    std::string base_url = "https://en.cppreference.com";
    int timeout_seconds = 10;
    int rate_limit_retry_delay_ms = 1000;
    int max_retries = 3;
};

struct ContentConfig {
    size_t max_output_chars = 8000;
};

struct ServerConfig {
    size_t thread_pool_size = 10;
    unsigned int http_port = 0;
};

class Config {
public:
    explicit Config(std::string_view config_path);
    ~Config() = default;
    Config(const Config&) = default;
    Config& operator=(const Config&) = default;
    Config(Config&&) = default;
    Config& operator=(Config&&) = default;

    const CacheConfig& get_cache_config() const { return cache_config_; }
    const ClientConfig& get_client_config() const { return client_config_; }
    const ContentConfig& get_content_config() const { return content_config_; }
    const ServerConfig& get_server_config() const { return server_config_; }

private:
    CacheConfig cache_config_;
    ClientConfig client_config_;
    ContentConfig content_config_;
    ServerConfig server_config_;
};

} // namespace cppreference::config

#endif // CPPREFERENCE_CONFIG_HPP
