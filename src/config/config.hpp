#ifndef CPPREFERENCE_CONFIG_HPP
#define CPPREFERENCE_CONFIG_HPP

#include "field_reflection.hpp"
#include "configuration.hpp"
#include <string>
#include <string_view>
#include <tuple>

namespace cppreference::config
{
    struct CacheConfig : Reflectable<CacheConfig>
    {
        bool enabled = true;
        std::string path = "./cache.db";
        int64_t default_ttl_seconds = 300;
        size_t max_size_mb = 50;

        static constexpr auto fields()
        {
            return std::tuple{
                Field{"enabled", &CacheConfig::enabled},
                Field{"path", &CacheConfig::path},
                Field{"default_ttl_seconds", &CacheConfig::default_ttl_seconds},
                Field{"max_size_mb", &CacheConfig::max_size_mb}
            };
        }
    };

    struct ClientConfig : Reflectable<ClientConfig>
    {
        std::string base_url = "https://en.cppreference.com";
        int timeout_seconds = 10;
        int rate_limit_retry_delay_ms = 1000;
        int max_retries = 3;

        static constexpr auto fields()
        {
            return std::tuple{
                Field{"base_url", &ClientConfig::base_url},
                Field{"timeout_seconds", &ClientConfig::timeout_seconds},
                Field{"rate_limit_retry_delay_ms", &ClientConfig::rate_limit_retry_delay_ms},
                Field{"max_retries", &ClientConfig::max_retries}
            };
        }
    };

    struct ContentConfig : Reflectable<ContentConfig>
    {
        bool dump_files = false;

        static constexpr auto fields()
        {
            return std::tuple{
                Field{"dump_files", &ContentConfig::dump_files}
            };
        }
    };

    struct ServerConfig : Reflectable<ServerConfig>
    {
        size_t thread_pool_size = 10;
        unsigned int http_port = 0;

        static constexpr auto fields()
        {
            return std::tuple{
                Field{"thread_pool_size", &ServerConfig::thread_pool_size},
                Field{"http_port", &ServerConfig::http_port}
            };
        }
    };

    struct ApplicationConfig : Reflectable<ApplicationConfig>
    {
        CacheConfig cache;
        ClientConfig client;
        ContentConfig content;
        ServerConfig server;

        static constexpr auto fields()
        {
            return std::tuple{
                Field{"cache", &ApplicationConfig::cache},
                Field{"client", &ApplicationConfig::client},
                Field{"content", &ApplicationConfig::content},
                Field{"server", &ApplicationConfig::server}
            };
        }
    };

    class Config
    {
    public:
        explicit Config(std::string_view config_path);
        ~Config() = default;
        Config(const Config&) = default;
        Config& operator=(const Config&) = default;
        Config(Config&&) = default;
        Config& operator=(Config&&) = default;

        [[nodiscard]] const CacheConfig& get_cache_config() const { return app_config_.cache; }
        [[nodiscard]] const ClientConfig& get_client_config() const { return app_config_.client; }
        [[nodiscard]] const ContentConfig& get_content_config() const { return app_config_.content; }
        [[nodiscard]] const ServerConfig& get_server_config() const { return app_config_.server; }

    private:
        ApplicationConfig app_config_;
    };
} // namespace cppreference::config

#endif // CPPREFERENCE_CONFIG_HPP
