#include "client.hpp"
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <nlohmann/json.hpp>
#include <regex>
#include <sstream>
#include <thread>
#include <vector>

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

std::pair<std::optional<std::string>, int> Client::search(std::string_view query) {
    int retries = 0;
    const int max_attempts = max_retries_ + 1;

    for (int attempt = 0; attempt < max_attempts; ++attempt) {
        auto res = [this, &query]() {
            std::lock_guard<std::mutex> lock{http_mutex_};
            return http_->Get("/index.php?search=" + url_encode(query));
        }();

        if (!res) {
            if (attempt < max_attempts - 1) {
                // Exponential backoff: retry_delay * 2^attempt
                auto backoff = retry_delay_ * (1 << attempt);
                std::this_thread::sleep_for(backoff);
                retries++;
                continue;
            }
            return {std::nullopt, retries};
        }

        if (res->status == http_status_too_many_requests || res->status == http_status_service_unavailable || res->status == 522) {
            if (attempt < max_attempts - 1) {
                // Exponential backoff
                auto backoff = retry_delay_ * (1 << attempt);
                std::this_thread::sleep_for(backoff);
                retries++;
                continue;
            }
            return {std::nullopt, retries};
        }

        if (res->status != http_status_ok) {
            return {std::nullopt, retries};
        }

        // Look for search results in <ul class="mw-search-results">
        std::size_t results_list = res->body.find("mw-search-results");
        if (results_list != std::string::npos) {
            // Find all <a href="/cpp/..." links within search results
            std::size_t pos = results_list;
            while (pos != std::string::npos) {
                std::size_t heading = res->body.find("mw-search-result-heading", pos);
                if (heading == std::string::npos) break;

                // Find the <a> tag after the heading
                std::size_t link_start = res->body.find("href=\"", heading);
                if (link_start == std::string::npos) break;
                link_start += 6;

                std::size_t href_end = res->body.find("\"", link_start);
                if (href_end == std::string::npos) break;

                std::string href = res->body.substr(link_start, href_end - link_start);

                if (href.starts_with("/cpp/")) {
                    // Skip headers and experimental
                    if (href.starts_with("/cpp/header/") || href.starts_with("/cpp/experimental/")) {
                        pos = heading + 1;
                        continue;
                    }
                    return {href.substr(1), retries}; // Remove leading /
                }

                pos = heading + 1;
            }
        }

        // Fallback: look for any /cpp/ link in the page
        std::size_t pos = 0;
        while (pos != std::string::npos) {
            std::size_t link_start = res->body.find("href=\"", pos);
            if (link_start == std::string::npos) break;
            link_start += 6;

            std::size_t href_end = res->body.find("\"", link_start);
            if (href_end == std::string::npos) break;

            std::string href = res->body.substr(link_start, href_end - link_start);

            if (href.starts_with("/cpp/") && !href.starts_with("/cpp/header/") && !href.starts_with("/cpp/experimental/")) {
                return {href.substr(1), retries};
            }

            pos = href_end + 1;
        }

        return {std::nullopt, retries};
    }

    return {std::nullopt, retries};
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

std::optional<std::string> Client::get_page_html(std::string_view title) {
    for (int attempt = 0; attempt <= max_retries_; ++attempt) {
        auto res = [this, &title]() {
            std::lock_guard<std::mutex> lock{http_mutex_};
            return http_->Get("/" + std::string(title));
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

        return res->body;
    }

    return std::nullopt;
}

} // namespace cppreference::client
