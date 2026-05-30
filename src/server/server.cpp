#include "server.hpp"
#include <nlohmann/json.hpp>
#include <format>
#include <iostream>

namespace cppreference::server {

void Server::log_message(const char* message)
{
    std::cerr << message << '\n';
}

Server::Server(const config::ClientConfig& client_config)
    : client_{client_config.base_url}
{
    (void)client_config;
}

void Server::run()
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);

    log_message("Starting cppreference MCP server");

    std::string line;
    while (std::getline(std::cin, line))
    {
        if (line.empty()) continue;

        try
        {
            auto request = nlohmann::json::parse(line);

            if (!request.is_object()) continue;
            if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0") continue;

            auto id = request.contains("id") ? request["id"] : nlohmann::json(nullptr);
            std::string method{request.value("method", "")};
            auto params = request.value("params", nlohmann::json::object());

            bool is_notification = !request.contains("id");
            if (is_notification) continue;

            nlohmann::json response{};
            response["jsonrpc"] = "2.0";
            response["id"] = id;

            if (method == "initialize")
                handle_initialize(response);
            else if (method == "tools/list")
                handle_tools_list(response);
            else if (method == "tools/call")
                handle_tools_call(response, params, client_);
            else
                response["error"] = {{"code", -32601}, {"message", std::format("Method not found: {}", method)}};

            std::cout << response.dump() << '\n';
            std::cout.flush();
        }
        catch (const nlohmann::json::parse_error&)
        {
            // malformed line — skip it
        }
        catch (const std::exception& e)
        {
            nlohmann::json error{
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32603}, {"message", e.what()}}}
            };
            std::cout << error.dump() << '\n';
            std::cout.flush();
        }
    }
}

void Server::handle_initialize(nlohmann::json& response)
{
    response["result"] = {
        {"protocolVersion", "2024-11-05"},
        {
            "capabilities", {
                {"tools", {{"listChanged", false}}},
                {"resources", nlohmann::json::object()}
            }
        },
        {
            "serverInfo", {
                {"name", "cppreference-mcp"},
                {"version", "1.0.0"}
            }
        }
    };
}

void Server::handle_tools_list(nlohmann::json& response)
{
    response["result"] = {
        {
            "tools", {
                {
                    {"name", "cppreference/lookup"},
                    {"description", "Search cppreference.com for C++ documentation"},
                    {
                        "inputSchema", {
                            {"type", "object"},
                            {
                                "properties", {
                                    {"query", {{"type", "string"}, {"description", "C++ symbol or keyword"}}}
                                }
                            },
                            {"required", {"query"}}
                        }
                    }
                }
            }
        }
    };
}

void Server::handle_tools_call(nlohmann::json& response, const nlohmann::json& params, const client::Client& client)
{
    auto tool_name = params.value("name", "");
    auto tool_params = params.value("arguments", nlohmann::json::object());
    std::string query{tool_params.value("query", "")};

    if (query.empty())
    {
        response["error"] = {{"code", -32602}, {"message", "Missing 'query' parameter"}};
    }
    else
    {
        auto title = client.search(query).value_or(query);
        auto content = client.get_page_content(title).value_or("No documentation found.");

        std::string truncated;
        if (content.size() > 2000)
        {
            truncated = content.substr(0, 2000) + "... [truncated]";
        }
        else
        {
            truncated = content;
        }

        response["result"] = {
            {
                "content", {
                    {
                        {"type", "text"},
                        {"text", truncated}
                    }
                }
            }
        };
    }
}

} // namespace cppreference::server
