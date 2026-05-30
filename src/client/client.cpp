#include "client.hpp"
#include <nlohmann/json.hpp>
#include <sstream>
#include <iomanip>
#include <thread>
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

Client::Client(std::string base_url, std::chrono::seconds timeout, int max_retries, int retry_delay_ms)
    : base_url_{std::move(base_url)}
    , timeout_{timeout}
    , max_retries_{max_retries}
    , retry_delay_{retry_delay_ms}
    , http_{std::make_unique<httplib::Client>(base_url_)}
{
    http_->set_follow_location(true);
    http_->set_connection_timeout(timeout_);
    http_->set_read_timeout(timeout_);
    http_->set_default_headers({{"User-Agent", "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"}});
}

std::optional<std::string> Client::search(const std::string& query) {
    static const std::regex link_regex(R"delim(<a[^>]*href="(/cpp/[^"]+))delim");

    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        auto res = [this, &query]() {
            std::lock_guard<std::mutex> lock{http_mutex_};
            return http_->Get("/index.php?title=Special:Search&search=" + url_encode(query));
        }();

        if (!res) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status == 429 || res->status == 503) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status != 200) {
            return std::nullopt;
        }

        auto begin = std::sregex_iterator(res->body.begin(), res->body.end(), link_regex);
        auto end = std::sregex_iterator();

        for (auto it = begin; it != end; ++it) {
            std::string href = (*it)[1].str();
            if (href.size() > 1) {
                return href.substr(1);
            }
        }
    }

    return std::nullopt;
}

std::optional<std::string> Client::get_page_content(const std::string& title) {
    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        auto res = [this, &title]() {
            std::lock_guard<std::mutex> lock{http_mutex_};
            return http_->Get("/api.php?action=query&prop=revisions&rvprop=content&format=json&titles="
                + url_encode(title));
        }();

        if (!res) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status == 429 || res->status == 503) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status != 200) {
            return std::nullopt;
        }

        try {
            auto json = nlohmann::json::parse(res->body);
            auto pages = json.value("query", nlohmann::json::object()).value("pages", nlohmann::json::object());
            for (auto& [page_id, page] : pages.items()) {
                if (page.contains("revisions") && !page["revisions"].empty()) {
                    auto revisions = page["revisions"];
                    if (!revisions.empty() && revisions[0].contains("*")) {
                        return revisions[0]["*"].get<std::string>();
                    }
                }
            }
        } catch (const std::exception&) {}
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace cppreference::client
