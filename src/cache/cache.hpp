#ifndef CPPREFERENCE_CACHE_CACHE_HPP
#define CPPREFERENCE_CACHE_CACHE_HPP

#include <string>
#include <optional>
#include <cstdint>
#include <memory>
#include <mutex>
#include <atomic>
#include "db/sqlite_db.hpp"

namespace cppreference::cache {

class Cache {
public:
    explicit Cache(const std::string& db_path, int64_t ttl_seconds, size_t max_size_mb);

    [[nodiscard]] std::optional<std::string> get(const std::string& key);
    bool put(const std::string& key, const std::string& value);
    bool remove(const std::string& key);
    bool evict_expired();
    [[nodiscard]] size_t size_bytes() const;
    [[nodiscard]] size_t row_count() const;

private:
    std::string db_path_;
    int64_t ttl_seconds_;
    size_t max_size_mb_;
    std::unique_ptr<db::SqliteDb> db_;
    mutable std::mutex mutex_;
    std::atomic<size_t> get_call_count_{0};
};

} // namespace cppreference::cache

#endif // CPPREFERENCE_CACHE_CACHE_HPP
