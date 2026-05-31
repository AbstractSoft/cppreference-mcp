#ifndef CPPREFERENCE_CACHE_HPP
#define CPPREFERENCE_CACHE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

#include "db/sqlite_db.hpp"

namespace cppreference::cache
{
    class Cache
    {
    public:
        Cache(std::string_view db_path, int64_t ttl_seconds, std::size_t max_size_mb,
              std::size_t evict_check_interval = 50);
        ~Cache() = default;
        Cache(const Cache&) = delete;
        Cache& operator=(const Cache&) = delete;
        Cache(Cache&&) = delete;
        Cache& operator=(Cache&&) = delete;

        std::optional<std::string> get(std::string_view key);
        bool put(std::string_view key, std::string_view value);
        bool remove(std::string_view key);
        void evict_expired();
        std::size_t size_bytes();
        std::size_t row_count();

    private:
        int64_t ttl_seconds_;
        std::size_t max_size_mb_;
        std::size_t evict_check_interval_;
        std::unique_ptr<db::SqliteDb> db_;
        mutable std::mutex mutex_;
        std::atomic<int64_t> get_call_count_{0};
    };
} // namespace cppreference::cache

#endif // CPPREFERENCE_CACHE_HPP
