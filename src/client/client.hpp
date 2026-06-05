#ifndef CPPREFERENCE_CLIENT_HPP
#define CPPREFERENCE_CLIENT_HPP

#include <chrono>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

#include <httplib.h>

namespace cppreference::client {

class Client {
public:
    // max_retries: number of extra attempts after the first request
    Client(std::string base_url, std::chrono::seconds timeout, int max_retries, int retry_delay_ms);
    ~Client() = default;
    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) = delete;
    Client& operator=(Client&&) = delete;

    // Returns the first matching cppreference page title (e.g., "cpp/algorithm/find")
    // Returns {title, retries} where retries is the number of retry attempts made
    std::pair<std::optional<std::string>, int> search(std::string_view query);
    // Returns raw MediaWiki wikitext for a given page title
    std::optional<std::string> get_page_content(std::string_view title);
    // Returns rendered HTML for a given page title
    std::optional<std::string> get_page_html(std::string_view title);

private:
    std::string base_url_;
    std::chrono::seconds timeout_;
    int max_retries_;
    std::chrono::milliseconds retry_delay_;
    std::unique_ptr<httplib::Client> http_;
    std::mutex http_mutex_;
};

} // namespace cppreference::client

#endif // CPPREFERENCE_CLIENT_HPP
