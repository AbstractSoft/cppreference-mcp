#include "cache.hpp"

namespace cppreference::cache {

Cache::Cache(const std::string& db_path, int64_t ttl_seconds, size_t max_size_mb)
    : db_path_{db_path}
    , ttl_seconds_{ttl_seconds}
    , max_size_mb_{max_size_mb}
    , db_{std::make_unique<db::SqliteDb>(db_path)}
{}

std::optional<std::string> Cache::get(const std::string& key) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return std::nullopt;
    
    ++get_call_count_;
    if (get_call_count_ % 50 == 0) {
        db_->evict_expired();
    }
    
    return db_->get(key);
}

bool Cache::put(const std::string& key, const std::string& value) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return false;
    
    if (max_size_mb_ > 0) {
        size_t current_size = db_->size_bytes();
        size_t max_size = static_cast<size_t>(max_size_mb_) * 1024u * 1024u;
        
        if (current_size > max_size) {
            size_t row_count = db_->row_count();
            size_t avg_row_size = row_count > 0 ? current_size / row_count : 1;
            size_t overage = current_size - max_size;
            size_t to_evict = std::max(size_t{1}, (overage * 11) / (avg_row_size * 10));
            db_->evict_oldest(to_evict);
        }
    }
    
    bool success = db_->put(key, value, ttl_seconds_);
    db_->checkpoint();
    
    return success;
}

bool Cache::remove(const std::string& key) {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return false;
    return db_->remove(key);
}

bool Cache::evict_expired() {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return false;
    return db_->evict_expired();
}

size_t Cache::size_bytes() const {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return 0;
    return db_->size_bytes();
}

size_t Cache::row_count() const {
    std::lock_guard<std::mutex> lock{mutex_};
    if (!db_) return 0;
    return db_->row_count();
}

} // namespace cppreference::cache
