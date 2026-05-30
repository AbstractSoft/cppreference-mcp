#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <memory>

namespace cppreference::db {
    class SqliteDb;
}

namespace cppreference::cache {

class Cache {
public:
    explicit Cache(const std::string& db_path, int64_t ttl_seconds, size_t max_size_mb);

    [[nodiscard]] std::optional<std::string> get(const std::string& key);
    bool put(const std::string& key, const std::string& value);
    bool remove(const std::string& key);
    bool evict_expired();
    [[nodiscard]] size_t size_bytes() const;

private:
    std::string db_path_;
    int64_t ttl_seconds_;
    size_t max_size_mb_;
    std::unique_ptr<db::SqliteDb> db_;
};

} // namespace cppreference::cache
