#include "cache.hpp"
#include "db/sqlite_db.hpp"

namespace cppreference::cache {

Cache::Cache(const std::string& db_path, int64_t ttl_seconds, size_t max_size_mb)
    : db_path_{db_path}
    , ttl_seconds_{ttl_seconds}
    , max_size_mb_{max_size_mb}
{
    db_ = std::make_unique<db::SqliteDb>(db_path);
}

std::optional<std::string> Cache::get(const std::string& key) {
    if (!db_) return std::nullopt;
    return db_->get(key);
}

bool Cache::put(const std::string& key, const std::string& value) {
    if (!db_) return false;
    
    bool success = db_->put(key, value, ttl_seconds_);
    
    if (success && max_size_mb_ > 0) {
        size_t current_size = db_->size_bytes();
        size_t max_size = static_cast<size_t>(max_size_mb_) * 1024u * 1024u;
        
        if (current_size > max_size) {
            size_t to_evict = static_cast<size_t>(std::max(size_t{1}, ((current_size - max_size) * 2) / 1024));
            db_->evict_oldest(to_evict);
        }
    }
    
    return success;
}

bool Cache::remove(const std::string& key) {
    if (!db_) return false;
    return db_->remove(key);
}

bool Cache::evict_expired() {
    if (!db_) return false;
    return db_->evict_expired();
}

size_t Cache::size_bytes() const {
    if (!db_) return 0;
    return db_->size_bytes();
}

} // namespace cppreference::cache
