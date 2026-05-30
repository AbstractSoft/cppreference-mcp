#pragma once

#include <string>
#include <optional>
#include <unordered_map>
#include <chrono>

namespace cppreference::client {

class Client {
public:
    explicit Client(std::string base_url);

    [[nodiscard]] std::optional<std::string> search(const std::string& query) const;
    [[nodiscard]] std::optional<std::string> get_page_content(const std::string& title) const;

private:
    std::string base_url_;
    std::chrono::seconds timeout_{5};
};

} // namespace cppreference::client
