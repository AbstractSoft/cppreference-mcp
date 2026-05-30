#pragma once

#include <nlohmann/json.hpp>
#include "client/client.hpp"
#include "config/config.hpp"

namespace cppreference::server {

class Server {
public:
    explicit Server(const config::ClientConfig& client_config);
    void run();

private:
    client::Client client_;
    static void handle_initialize(nlohmann::json& response);
    static void handle_tools_list(nlohmann::json& response);
    static void handle_tools_call(nlohmann::json& response, const nlohmann::json& params, const client::Client& client);
    static void log_message(const char* message);
};

} // namespace cppreference::server
