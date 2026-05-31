#include "server.hpp"

#include <algorithm>
#include <csignal>
#include <format>
#include <iostream>
#include <nlohmann/json.hpp>

#include "parser/parser.hpp"

namespace cppreference::server
{
    namespace
    {
        constexpr int http_status_ok = 200;
        constexpr int http_status_bad_request = 400;
        constexpr int http_status_method_not_allowed = 405;
        constexpr int http_status_internal_error = 500;

        std::string json_to_string(const nlohmann::json& j)
        {
            return j.dump();
        }

        httplib::Server* g_http_server = nullptr;

        void shutdown_handler(int)
        {
            if (g_http_server)
            {
                g_http_server->stop();
            }
        }
    } // namespace

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
        unsigned int http_port = cfg.get_server_config().http_port;
        if (http_port > 0)
        {
            http_server_ = std::make_unique<httplib::Server>();

            http_server_->set_error_handler([](const httplib::Request&, httplib::Response& res)
            {
                res.set_content("Internal Server Error", "text/plain");
                res.status = http_status_internal_error;
            });

            http_server_->Post("/", [this](const httplib::Request& req, httplib::Response& res)
            {
                handle_http_request(req, res);
            });

            g_http_server = http_server_.get();
        }
    }

    Server::~Server()
    {
        shutdown();
    }

    void Server::shutdown()
    {
        if (http_server_)
        {
            http_server_->stop();
            http_server_.reset();
            g_http_server = nullptr;
        }
    }

    void Server::run()
    {
        std::setvbuf(stdout, nullptr, _IONBF, 0);
        std::setvbuf(stderr, nullptr, _IONBF, 0);

        log_message("Starting cppreference MCP server");

        unsigned int http_port = config_.get_server_config().http_port;
        if (http_port > 0)
        {
            log_message((std::string{"HTTP server starting on port "} + std::to_string(http_port)).c_str());
            log_message((std::string{"Cache initialized: "} + config_.get_cache_config().path).c_str());

            signal(SIGINT, shutdown_handler);
            signal(SIGTERM, shutdown_handler);

            bool listen_ok = http_server_->listen("", http_port);
            if (!listen_ok)
            {
                log_message("Failed to start HTTP server");
                g_http_server = nullptr;
                return;
            }

            g_http_server = nullptr;
        }
        else
        {
            log_message(
                (std::string{"Client timeout: "} + std::to_string(config_.get_client_config().timeout_seconds) + "s").
                c_str());
            log_message(
                (std::string{"Cache enabled: "} + std::string{
                    config_.get_cache_config().enabled ? "yes" : "no"
                }).c_str());
            log_message((std::string{"Max output chars: "} + std::to_string(max_output_chars_)).c_str());
            log_message((std::string{"Cache initialized: "} + config_.get_cache_config().path).c_str());

            // stdio mode
            std::string line;
            while (std::getline(std::cin, line))
            {
                if (line.empty())
                {
                    continue;
                }

                try
                {
                    auto request = nlohmann::json::parse(line);
                    nlohmann::json response = handle_request(request);
                    std::cout << json_to_string(response) << '\n';
                    std::cout.flush();
                }
                catch (const nlohmann::json::parse_error& /*e*/)
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
                    std::cout << json_to_string(error) << '\n';
                    std::cout.flush();
                }
            }
        }
    }

    nlohmann::json Server::handle_request(const nlohmann::json& request)
    {
        nlohmann::json response{};
        response["jsonrpc"] = "2.0";

        if (!request.is_object())
        {
            response["error"] = {{"code", -32700}, {"message", "Invalid request"}};
            response["id"] = nullptr;
            return response;
        }

        if (!request.contains("jsonrpc") || request["jsonrpc"] != "2.0")
        {
            response["error"] = {{"code", -32600}, {"message", "Invalid JSON-RPC version"}};
            response["id"] = request.contains("id") ? request["id"] : nullptr;
            return response;
        }

        auto request_id = request.contains("id") ? request["id"] : nlohmann::json(nullptr);
        response["id"] = request_id;

        if (!request.contains("method") || !request["method"].is_string())
        {
            response["error"] = {{"code", -32601}, {"message", "Method not found"}};
            return response;
        }

        std::string method = request["method"].get<std::string>();
        auto params = request.value("params", nlohmann::json::object());

        if (method == "initialize")
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
        else if (method == "tools/list")
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
        else if (method == "tools/call")
        {
            auto tool_name = params.value("name", "");
            if (tool_name != "cppreference/lookup")
            {
                response["error"] = {{"code", -32601}, {"message", std::format("Tool not found: {}", tool_name)}};
                return response;
            }

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
        else
        {
            response["error"] = {{"code", -32601}, {"message", std::format("Method not found: {}", method)}};
        }

        return response;
    }

    void Server::handle_http_request(const httplib::Request& req, httplib::Response& res)
    {
        if (req.method != "POST")
        {
            res.set_content("Method Not Allowed", "text/plain");
            res.status = http_status_method_not_allowed;
            return;
        }

        std::string content_type;
        if (req.has_header("Content-Type"))
        {
            content_type = req.get_header_value("Content-Type");
        }

        if (content_type.find("application/json") == std::string::npos)
        {
            res.set_content("Content-Type must be application/json", "text/plain");
            res.status = http_status_bad_request;
            return;
        }

        try
        {
            nlohmann::json request = nlohmann::json::parse(req.body);
            nlohmann::json response = handle_request(request);

            res.set_content(json_to_string(response), "application/json");
            res.status = http_status_ok;
        }
        catch (const nlohmann::json::parse_error& e)
        {
            nlohmann::json error{
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32700}, {"message", e.what()}}}
            };
            res.set_content(json_to_string(error), "application/json");
            res.status = http_status_bad_request;
        }
        catch (const std::exception& e)
        {
            nlohmann::json error{
                {"jsonrpc", "2.0"},
                {"id", nullptr},
                {"error", {{"code", -32603}, {"message", e.what()}}}
            };
            res.set_content(json_to_string(error), "application/json");
            res.status = http_status_internal_error;
        }
    }

    nlohmann::json Server::handle_tools_call_sync(const nlohmann::json& params)
    {
        auto tool_params = params.value("arguments", nlohmann::json::object());
        std::string query{tool_params.value("query", "")};

        if (query.empty())
        {
            return nlohmann::json{
                {"content", {{{"type", "text"}, {"text", "Missing 'query' parameter"}}}}
            };
        }

        auto title = client_.search(query).value_or(query);

        std::string content;
        bool has_cache = false;

        if (config_.get_cache_config().enabled)
        {
            auto cached = cache_.get("page:" + title);
            if (cached.has_value())
            {
                content = std::move(cached.value());
                has_cache = true;
                log_message((std::string{"Cache hit for: "} + title).c_str());
            }
            else
            {
                log_message((std::string{"Cache miss for: "} + title).c_str());
            }
        }

        if (!has_cache)
        {
            std::string raw_wikitext = client_.get_page_content(title).value_or("No documentation found.");

            if (config_.get_cache_config().enabled && !raw_wikitext.empty() && raw_wikitext !=
                "No documentation found.")
            {
                content = parser::convert(raw_wikitext);
                cache_.put("page:" + title, content);
            }
            else
            {
                content = std::move(raw_wikitext);
            }
        }

        if (content.empty())
        {
            content = "No documentation found.";
        }

        if (content.size() > max_output_chars_)
        {
            content = content.substr(0, max_output_chars_) + "... [truncated]";
        }

        return nlohmann::json{
            {"content", {{{"type", "text"}, {"text", content}}}}
        };
    }

    void Server::log_message(const char* message)
    {
        std::lock_guard<std::mutex> lock{log_mutex_};
        std::cerr << message << '\n';
    }
} // namespace cppreference::server
