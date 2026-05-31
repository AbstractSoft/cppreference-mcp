#include "cache.hpp"

namespace cppreference::cache
{
    Cache::Cache(std::string_view db_path, int64_t ttl_seconds, std::size_t max_size_mb,
                 std::size_t evict_check_interval)
        : ttl_seconds_{ttl_seconds}
          , max_size_mb_{max_size_mb}
          , evict_check_interval_{evict_check_interval}
          , db_{std::make_unique<db::SqliteDb>(db_path)}
    {
    }

    std::optional<std::string> Cache::get(std::string_view key)
    {
        if (++get_call_count_ % evict_check_interval_ == 0)
        {
            std::lock_guard<std::mutex> lock{mutex_};
            (void)db_->evict_expired();
        }

        std::lock_guard<std::mutex> lock{mutex_};
        return db_->get(key);
    }

    bool Cache::put(std::string_view key, std::string_view value)
    {
        std::lock_guard<std::mutex> lock{mutex_};

        (void)db_->evict_expired();

        if (max_size_mb_ > 0)
        {
            const std::size_t max_bytes = max_size_mb_ * 1024ULL * 1024ULL;
            while (db_->size_bytes() > max_bytes)
            {
                if (!db_->evict_oldest(1))
                {
                    return false;
                }
            }
        }

        return db_->put(key, value, ttl_seconds_);
    }

    bool Cache::remove(std::string_view key)
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return db_->remove(key);
    }

    void Cache::evict_expired()
    {
        std::lock_guard<std::mutex> lock{mutex_};
        (void)db_->evict_expired();
    }

    std::size_t Cache::size_bytes()
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return db_->size_bytes();
    }

    std::size_t Cache::row_count()
    {
        std::lock_guard<std::mutex> lock{mutex_};
        return db_->row_count();
    }
} // namespace cppreference::cache
