#include "server.hpp"

#include <algorithm>
#include <csignal>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <unordered_map>

#include "parser/html_to_md.hpp"

#include <cstdio>
#include <memory>
#include <sstream>
#include <stdexcept>

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
            try {
                return j.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
            } catch (const std::exception& e) {
                return std::string{"JSON serialization error: "} + e.what();
            }
        }

        // Map common queries to their cppreference page titles
        std::string query_to_title(const std::string& query)
        {
            static const std::unordered_map<std::string, std::string> map = {
                {"std::vector", "cpp/container/vector"},
                {"std::map", "cpp/container/map"},
                {"std::unordered_map", "cpp/container/unordered_map"},
                {"std::string", "cpp/string/basic_string"},
                {"std::shared_ptr", "cpp/memory/shared_ptr"},
                {"std::unique_ptr", "cpp/memory/unique_ptr"},
                {"std::thread", "cpp/thread/thread"},
                {"std::async", "cpp/thread/async"},
                {"std::future", "cpp/thread/future"},
                {"std::promise", "cpp/thread/promise"},
                {"std::atomic", "cpp/atomic/atomic"},
                {"std::mutex", "cpp/thread/mutex"},
                {"std::lock_guard", "cpp/thread/lock_guard"},
                {"std::unique_lock", "cpp/thread/unique_lock"},
                {"std::condition_variable", "cpp/thread/condition_variable"},
                {"std::function", "cpp/utility/functional/function"},
                {"std::bind", "cpp/utility/functional/bind"},
                {"std::make_shared", "cpp/memory/make_shared"},
                {"std::make_unique", "cpp/memory/make_unique"},
                {"std::optional", "cpp/utility/optional/optional"},
                {"std::variant", "cpp/utility/variant/variant"},
                {"std::any", "cpp/utility/any/any"},
                {"std::tuple", "cpp/utility/tuple/tuple"},
                {"std::pair", "cpp/utility/pair/pair"},
                {"std::array", "cpp/array/array"},
                {"std::deque", "cpp/container/deque"},
                {"std::list", "cpp/container/list"},
                {"std::forward_list", "cpp/container/forward_list"},
                {"std::set", "cpp/container/set"},
                {"std::unordered_set", "cpp/container/unordered_set"},
                {"std::stack", "cpp/container/stack"},
                {"std::queue", "cpp/container/queue"},
                {"std::priority_queue", "cpp/container/priority_queue"},
                {"std::algorithm", "cpp/algorithm/algorithm"},
                {"std::sort", "cpp/algorithm/sort"},
                {"std::find", "cpp/algorithm/find"},
                {"std::transform", "cpp/algorithm/transform"},
                {"std::copy", "cpp/algorithm/copy"},
                {"std::move", "cpp/utility/move/move"},
                {"std::forward", "cpp/utility/forward/forward"},
                {"std::swap", "cpp/utility/swap/swap"},
                {"std::enable_shared_from_this", "cpp/memory/enable_shared_from_this"},
                {"std::weak_ptr", "cpp/memory/weak_ptr"},
                {"std::iostream", "cpp/io/cin"},
                {"std::ifstream", "cpp/io/basic_ifstream"},
                {"std::ofstream", "cpp/io/basic_ofstream"},
                {"std::stringstream", "cpp/io/basic_stringstream"},
                {"std::filesystem", "cpp/filesystem/directory_iterator"},
                {"std::chrono", "cpp/chrono/chrono"},
                {"std::regex", "cpp/regex/regex"},
                {"uint64_t", "cpp/utility/integer/uint64_t"},
                {"int64_t", "cpp/utility/integer/int64_t"},
                {"int32_t", "cpp/utility/integer/int32_t"},
                {"int8_t", "cpp/utility/integer/int8_t"},
                {"size_t", "cpp/utility/integer/size_t"},
                {"ptrdiff_t", "cpp/utility/integer/ptrdiff_t"},
                {"nullptr_t", "cpp/utility/nullptr_t/nullptr_t"},
            };

            auto it = map.find(query);
            if (it != map.end())
            {
                return it->second;
            }

            // Strip "std::" prefix and try common namespaces
            std::string base = query;
            if (base.substr(0, 5) == "std::")
            {
                base = base.substr(5);
            }

            // Remove template parameters
            auto lt = base.find('<');
            if (lt != std::string::npos)
            {
                base = base.substr(0, lt);
            }

            // Try common namespaces
            if (base == "vector" || base == "map" || base == "set" || base == "deque" ||
                base == "list" || base == "array" || base == "stack" || base == "queue" ||
                base == "unordered_map" || base == "unordered_set" || base == "forward_list")
            {
                return "cpp/container/" + base;
            }
            if (base == "string" || base == "wstring")
            {
                return "cpp/string/basic_string";
            }
            if (base == "shared_ptr" || base == "unique_ptr" || base == "weak_ptr" ||
                base == "make_shared" || base == "make_unique" || base == "atomic" ||
                base == "enable_shared_from_this")
            {
                return "cpp/memory/" + base;
            }
            if (base == "thread" || base == "async" || base == "future" || base == "promise" ||
                base == "mutex" || base == "lock_guard" || base == "unique_lock" ||
                base == "condition_variable")
            {
                return "cpp/thread/" + base;
            }
            if (base == "optional" || base == "variant" || base == "any" || base == "tuple" ||
                base == "pair" || base == "function" || base == "bind" ||
                base == "move" || base == "forward" || base == "swap")
            {
                return "cpp/utility/" + base;
            }
            if (base == "algorithm" || base == "sort" || base == "find" || base == "transform" ||
                base == "copy")
            {
                return "cpp/algorithm/" + base;
            }
            if (base == "filesystem" || base == "chrono" || base == "regex")
            {
                return "cpp/" + base;
            }

            // Fallback: try cpp/ prefix
            return "cpp/" + base;
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
                {"resultType", "complete"},
                {"protocolVersion", "2026-07-28"},
                {
                    "capabilities", {
                        {"tools", {{"listChanged", false}}}
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
                {"resultType", "complete"},
                {
                    "tools", {
                        {
                            {"name", "cppreference-lookup"},
                            {"title", "C++ Reference Lookup"},
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
            if (tool_name != "cppreference-lookup")
            {
                response["error"] = {{"code", -32602}, {"message", std::format("Unknown tool: {}", tool_name)}};
                return response;
            }

            try
            {
                auto future = pool_.submit([this, params]() -> nlohmann::json
                {
                    try {
                        return handle_tools_call_sync(params);
                    } catch (const std::exception& e) {
                        log_message((std::string{"Exception in handle_tools_call_sync: "} + e.what()).c_str());
                        throw;
                    }
                });

                auto timeout = std::chrono::seconds{config_.get_client_config().timeout_seconds * 5};
                if (future.wait_for(timeout) != std::future_status::ready)
                {
                    response["result"] = {
                        {"resultType", "complete"},
                        {"content", {{{"type", "text"}, {"text", "Request timed out"}}}},
                        {"isError", true}
                    };
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
                {"resultType", "complete"},
                {"content", {{{"type", "text"}, {"text", "Missing 'query' parameter"}}}},
                {"isError", true}
            };
        }

        // Strip template arguments for search (e.g., "std::shared_ptr<int>" → "std::shared_ptr")
        std::string search_query = query;
        auto lt = search_query.find('<');
        if (lt != std::string::npos)
        {
            search_query = search_query.substr(0, lt);
        }

        auto [title_opt, retries] = client_.search(search_query);
        std::string title;

        if (title_opt.has_value())
        {
            title = title_opt.value();
            if (retries > 0)
            {
                log_message((std::string{"Search succeeded after "} + std::to_string(retries) + " retries").c_str());
            }
        }
        else
        {
            title = query_to_title(query);
            log_message((std::string{"Search unavailable, using fallback title: "} + title).c_str());
        }

        std::string content;
        std::string html;
        bool has_cache = false;
        int search_retries = retries;

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
            html = client_.get_page_html(title).value_or("");

            if (!html.empty())
            {
                try {
                    content = cppreference::parser::convert_html_to_markdown(html);
                    if (config_.get_cache_config().enabled) {
                        cache_.put("page:" + title, content);
                    }
                } catch (const std::exception& e) {
                    log_message((std::string{"Parser error: "} + e.what()).c_str());
                    content = "Error parsing documentation: " + std::string(e.what());
                }
            }
        }

        if (content.empty())
        {
            content = "No documentation found.";
        }

        if (config_.get_content_config().dump_files && !html.empty())
        {
            try {
                std::filesystem::create_directories("files");
                std::string safe_title = title;
                std::replace(safe_title.begin(), safe_title.end(), '/', '_');
                std::replace(safe_title.begin(), safe_title.end(), ' ', '_');

                std::string markdown_path = "files/" + safe_title + ".md";
                std::string html_path = "files/" + safe_title + ".html";

                std::ofstream(markdown_path) << content;
                std::ofstream(html_path) << html;

                log_message((std::string{"Dumped: "} + markdown_path + ", and " + html_path).c_str());
            } catch (const std::exception& e) {
                log_message((std::string{"File dump error: "} + e.what()).c_str());
            }
        }

        bool is_error = (content == "No documentation found.");
        return nlohmann::json{
            {"resultType", "complete"},
            {"content", {{{"type", "text"}, {"text", content}}}},
            {"isError", is_error}
        };
    }

    void Server::log_message(const char* message)
    {
        std::lock_guard<std::mutex> lock{log_mutex_};
        std::cerr << message << '\n';
    }
} // namespace cppreference::server
