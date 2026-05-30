#ifndef CPPREFERENCE_CLIENT_CLIENT_HPP
#define CPPREFERENCE_CLIENT_CLIENT_HPP

#include <string>
#include <optional>
#include <chrono>
#include <memory>
#include <httplib.h>

namespace cppreference::client {

class Client {
public:
    explicit Client(std::string base_url, std::chrono::seconds timeout, int max_retries, int retry_delay_ms);

    [[nodiscard]] std::optional<std::string> search(const std::string& query);
    [[nodiscard]] std::optional<std::string> get_page_content(const std::string& title);

private:
    std::string base_url_;
    std::chrono::seconds timeout_;
    int max_retries_;
    std::chrono::milliseconds retry_delay_;
    std::unique_ptr<httplib::Client> http_;
    mutable std::mutex http_mutex_;
};

} // namespace cppreference::client

#endif // CPPREFERENCE_CLIENT_CLIENT_HPP
