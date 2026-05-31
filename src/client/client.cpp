#include "client.hpp"
#include <iomanip>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <thread>

namespace cppreference::client {

namespace {

constexpr int http_status_ok = 200;
constexpr int http_status_too_many_requests = 429;
constexpr int http_status_service_unavailable = 503;

// RFC 3986 percent‑encoding; safe for path and query strings.
std::string url_encode(std::string_view value) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;

    for (unsigned char c : value) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << std::uppercase << '%' << std::setw(2) << static_cast<int>(c) << std::nouppercase;
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

std::optional<std::string> Client::search(std::string_view query) {
    // WARNING: This regex scrapes HTML. It may break if cppreference changes its search page layout.
    // The official MediaWiki API search is broken on cppreference, so this is a necessary workaround.
    static const std::regex link_regex(R"delim(<a[^>]*href="(/cpp/[^"]+))delim");

    // First attempt is try #0, then up to max_retries_ additional retries
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

        if (res->status == http_status_too_many_requests || res->status == http_status_service_unavailable) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status != http_status_ok) {
            return std::nullopt;
        }

        auto begin = std::sregex_iterator(res->body.begin(), res->body.end(), link_regex);
        auto end = std::sregex_iterator();

        std::string preferred;
        for (auto it = begin; it != end; ++it) {
            std::string href = (*it)[1].str();
            if (href.size() > 1) {
                std::string title = href.substr(1);
                if (!title.starts_with("cpp/header/") && !title.starts_with("cpp/experimental/")) {
                    return title;
                }
                if (preferred.empty() && title.starts_with("cpp/")) {
                    preferred = title;
                }
            }
        }
        if (!preferred.empty()) {
            return preferred;
        }
    }

    return std::nullopt;
}

std::optional<std::string> Client::get_page_content(std::string_view title) {
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

        if (res->status == http_status_too_many_requests || res->status == http_status_service_unavailable) {
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        if (res->status != http_status_ok) {
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
        } catch (const std::exception&) {
            // JSON parse error – treat as transient and retry
            if (attempt < max_retries_) {
                std::this_thread::sleep_for(retry_delay_);
                continue;
            }
            return std::nullopt;
        }

        // No revisions found → page does not exist or has no content
        return std::nullopt;
    }

    return std::nullopt;
}

} // namespace cppreference::client
