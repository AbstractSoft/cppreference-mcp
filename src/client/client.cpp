#include "client.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <regex>

namespace cppreference::client {

namespace {

std::string url_encode(const std::string& value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (auto c : value) {
        if (std::isalnum(static_cast<unsigned char>(c)) ||
            c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase;
            escaped << '%' << std::setw(2) << static_cast<int>(static_cast<unsigned char>(c));
            escaped << std::nouppercase;
        }
    }

    return escaped.str();
}

} // namespace

Client::Client(std::string base_url) : base_url_{std::move(base_url)} {}

std::optional<std::string> Client::search(const std::string& query) const {
    httplib::Client client{base_url_};
    client.set_follow_location(true);
    client.set_connection_timeout(timeout_);
    client.set_read_timeout(timeout_);
    client.set_default_headers({{"User-Agent", "cppreference-mcp/1.0 (C++23)"}});

    auto res = client.Get("/index.php?title=Special:Search&search=" + url_encode(query));

    if (!res) return std::nullopt;
    if (res->status != 200) return std::nullopt;

    std::regex link_regex("<a href=\"/([^/][^\"]+)\" title=\"([^\"]+)\">");
    auto begin = std::sregex_iterator(res->body.begin(), res->body.end(), link_regex);
    auto end = std::sregex_iterator();

    for (auto it = begin; it != end; ++it) {
        std::string href = (*it)[1].str();
        std::string title = (*it)[2].str();
        if (href.find("cpp/") == 0 || href.find("w/") == 0) {
            return title;
        }
    }
    return std::nullopt;
}

std::optional<std::string> Client::get_page_content(const std::string& title) const {
    httplib::Client client{base_url_};
    client.set_follow_location(true);
    client.set_connection_timeout(timeout_);
    client.set_read_timeout(timeout_);
    client.set_default_headers({{"User-Agent", "cppreference-mcp/1.0 (C++23)"}});

    auto res = client.Get("/api.php?action=query&prop=revisions&rvprop=content&format=json&titles="
        + url_encode(title));

    if (!res) return std::nullopt;
    if (res->status != 200) return std::nullopt;

    try {
        auto json = nlohmann::json::parse(res->body);
        auto pages = json.value("query", nlohmann::json::object()).value("pages", nlohmann::json::object());
        for (auto& [page_id, page] : pages.items()) {
            if (page.contains("revisions") && !page["revisions"].empty()) {
                return page["revisions"][0]["*"].get<std::string>();
            }
        }
    } catch (const std::exception&) {}
    return std::nullopt;
}

} // namespace cppreference::client
