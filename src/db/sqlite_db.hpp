#ifndef CPPREFERENCE_DB_HPP
#define CPPREFERENCE_DB_HPP

#include <optional>
#include <sqlite3.h>
#include <string>
#include <string_view>

namespace cppreference::db
{
    class SqliteDb
    {
    public:
        explicit SqliteDb(std::string_view path);
        ~SqliteDb();
        SqliteDb(const SqliteDb&) = delete;
        SqliteDb& operator=(const SqliteDb&) = delete;
        SqliteDb(SqliteDb&&) = delete;
        SqliteDb& operator=(SqliteDb&&) = delete;

        [[nodiscard]] std::optional<std::string> get(std::string_view key);
        [[nodiscard]] bool put(std::string_view key, std::string_view value, int64_t ttl_seconds);
        [[nodiscard]] bool remove(std::string_view key);
        [[nodiscard]] bool evict_expired();
        [[nodiscard]] bool evict_oldest(size_t count);
        void checkpoint();
        [[nodiscard]] size_t size_bytes();
        [[nodiscard]] size_t row_count();

    private:
        static int64_t now_seconds();
        void init();

        std::string path_;
        sqlite3* db_ = nullptr;
        sqlite3_stmt* stmt_get_ = nullptr;
        sqlite3_stmt* stmt_put_ = nullptr;
        sqlite3_stmt* stmt_remove_ = nullptr;
        sqlite3_stmt* stmt_evict_expired_ = nullptr;
        sqlite3_stmt* stmt_evict_oldest_ = nullptr;
        sqlite3_stmt* stmt_size_ = nullptr;
        sqlite3_stmt* stmt_row_count_ = nullptr;
    };
} // namespace cppreference::db

#endif // CPPREFERENCE_DB_HPP
