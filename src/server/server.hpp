#ifndef CPPREFERENCE_SERVER_SERVER_HPP
#define CPPREFERENCE_SERVER_SERVER_HPP

#include <nlohmann/json.hpp>
#include <mutex>
#include "client/client.hpp"
#include "config/config.hpp"
#include "cache/cache.hpp"
#include "thread_pool/thread_pool.hpp"

namespace cppreference::server {

class Server {
public:
    explicit Server(const config::Config& cfg);
    void run();

private:
    config::Config config_;
    client::Client client_;
    cache::Cache cache_;
    thread_pool::ThreadPool pool_;
    size_t max_output_chars_;

    static void handle_initialize(nlohmann::json& response);
    static void handle_tools_list(nlohmann::json& response);
    void handle_tools_call(nlohmann::json& response, const nlohmann::json& params);
    nlohmann::json handle_tools_call_sync(const nlohmann::json& params);
    static void log_message(const char* message);
    static std::mutex log_mutex_;
};

} // namespace cppreference::server

#endif // CPPREFERENCE_SERVER_SERVER_HPP
