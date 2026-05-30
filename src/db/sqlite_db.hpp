#pragma once

#include <string>
#include <optional>
#include <cstdint>
#include <sqlite3.h>

namespace cppreference::db {

class SqliteDb {
public:
    explicit SqliteDb(const std::string& path);
    ~SqliteDb();

    SqliteDb(const SqliteDb&) = delete;
    SqliteDb& operator=(const SqliteDb&) = delete;
    SqliteDb(SqliteDb&&) = delete;
    SqliteDb& operator=(SqliteDb&&) = delete;

    std::optional<std::string> get(const std::string& key);
    bool put(const std::string& key, const std::string& value, int64_t ttl_seconds);
    bool remove(const std::string& key);
    bool evict_expired();
    bool evict_oldest(size_t count);
    [[nodiscard]] size_t size_bytes() const;

private:
    void init();
    int64_t now_seconds() const;

    std::string path_;
    sqlite3* db_ = nullptr;
    sqlite3_stmt* stmt_get_ = nullptr;
    sqlite3_stmt* stmt_put_ = nullptr;
    sqlite3_stmt* stmt_remove_ = nullptr;
    sqlite3_stmt* stmt_evict_expired_ = nullptr;
    sqlite3_stmt* stmt_evict_oldest_ = nullptr;
    sqlite3_stmt* stmt_size_ = nullptr;
};

} // namespace cppreference::db
