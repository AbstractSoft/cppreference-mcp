#ifndef CPPREFERENCE_SERVER_HPP
#define CPPREFERENCE_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <nlohmann/json.hpp>
#include <string>

#include "cache/cache.hpp"
#include "client/client.hpp"
#include "config/config.hpp"

#include <httplib.h>
#include "thread_pool.hpp"

namespace cppreference::server
{
    // Re-export from shared library for backward compatibility
    using thread_pool::ThreadPool;

    class Server
    {
    public:
        explicit Server(const config::Config& cfg);
        ~Server();
        void run();
        void shutdown();

    private:
        nlohmann::json handle_request(const nlohmann::json& request);
        void handle_http_request(const httplib::Request& req, httplib::Response& res);
        nlohmann::json handle_tools_call_sync(const nlohmann::json& params);
        void log_message(const char* message);

        config::Config config_;
        client::Client client_;
        cache::Cache cache_;
        ThreadPool pool_;

        std::unique_ptr<httplib::Server> http_server_;
        std::mutex log_mutex_;
    };
} // namespace cppreference::server

#endif // CPPREFERENCE_SERVER_HPP
