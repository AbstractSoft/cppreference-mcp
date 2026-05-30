#include "server.hpp"
#include <nlohmann/json.hpp>
#include <format>
#include <iostream>

namespace cppreference::server
{
    std::mutex Server::log_mutex_;

    void Server::log_message(const char* message)
    {
        std::lock_guard<std::mutex> lock{log_mutex_};
        std::cerr << message << '\n';
    }

    Server::Server(const config::Config& cfg)
        : config_{cfg}
          , client_{
              cfg.get_client_config().base_url,
              std::chrono::seconds{cfg.get_client_config().timeout_seconds},
              cfg.get_client_config().max_retries,
              cfg.get_client_config().rate_limit_retry_delay_ms
          }
          , cache_{
              cfg.get_cache_config().path,
              cfg.get_cache_config().default_ttl_seconds,
              cfg.get_cache_config().max_size_mb
          }
          , pool_{cfg.get_server_config().thread_pool_size}
          , max_output_chars_{cfg.get_content_config().max_output_chars}
    {
        log_message(
            (std::string{"Client timeout: "} + std::to_string(cfg.get_client_config().timeout_seconds) + "s").c_str());
        log_message(
            (std::string{"Cache enabled: "} + std::string{cfg.get_cache_config().enabled ? "yes" : "no"}).c_str());
        log_message((std::string{"Max output chars: "} + std::to_string(max_output_chars_)).c_str());
    }

    void Server::run()
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);

        log_message("Starting cppreference MCP server");

        if (config_.get_cache_config().enabled)
        {
            log_message((std::string{"Cache initialized: "} + config_.get_cache_config().path).c_str());
        }

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
                std::string method;

                if (request.contains("method") && request["method"].is_string())
                {
                    method = request["method"].get<std::string>();
                }
                else
                {
                    continue;
                }

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
                    handle_tools_call(response, params);
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

    void Server::handle_tools_call(nlohmann::json& response, const nlohmann::json& params)
    {
        auto tool_name = params.value("name", "");
        if (tool_name != "cppreference/lookup")
        {
            response["error"] = {{"code", -32601}, {"message", std::format("Tool not found: {}", tool_name)}};
            return;
        }

        auto tool_params = params.value("arguments", nlohmann::json::object());
        std::string query{tool_params.value("query", "")};

        if (query.empty())
        {
            response["error"] = {{"code", -32602}, {"message", "Missing 'query' parameter"}};
        }
        else
        {
            try
            {
                auto future = pool_.submit([this, params]() -> nlohmann::json
                {
                    return handle_tools_call_sync(params);
                });

                auto timeout = std::chrono::seconds{config_.get_client_config().timeout_seconds * 5};
                if (future.wait_for(timeout) != std::future_status::ready)
                {
                    response["error"] = {{"code", -32002}, {"message", "Request timed out"}};
                }
                else
                {
                    response["result"] = future.get();
                }
            }
            catch (const std::exception& e)
            {
                response["error"] = {
                    {"code", -32603}, {"message", (std::string{"Thread pool error: "} + e.what()).c_str()}
                };
            }
        }
    }

    nlohmann::json Server::handle_tools_call_sync(const nlohmann::json& params)
    {
        nlohmann::json response{};
        auto tool_params = params.value("arguments", nlohmann::json::object());
        std::string query{tool_params.value("query", "")};

        auto title = client_.search(query).value_or(query);

        std::string cached_content;
        bool has_cache = false;

        if (config_.get_cache_config().enabled)
        {
            auto cached = cache_.get("page:" + title);
            if (cached.has_value())
            {
                cached_content = std::move(cached.value());
                has_cache = true;
                log_message((std::string{"Cache hit for: "} + title).c_str());
            }
            else
            {
                log_message((std::string{"Cache miss for: "} + title).c_str());
            }
        }

        std::string content;
        if (!has_cache)
        {
            content = client_.get_page_content(title).value_or("No documentation found.");

            if (config_.get_cache_config().enabled && !content.empty() && content != "No documentation found.")
            {
                cache_.put("page:" + title, content);
            }
        }
        else
        {
            content = std::move(cached_content);
        }

        std::string truncated;
        if (content.size() > max_output_chars_)
        {
            truncated = content.substr(0, max_output_chars_) + "... [truncated]";
        }
        else
        {
            truncated = content;
        }

        return nlohmann::json{
            {"content", {{{"type", "text"}, {"text", truncated}}}}
        };
    }
} // namespace cppreference::server
