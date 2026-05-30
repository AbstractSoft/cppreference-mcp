#include "sqlite_db.hpp"
#include <chrono>
#include <stdexcept>
#include <sstream>

namespace cppreference::db {

namespace {

int64_t current_timestamp() {
    return std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

} // namespace

SqliteDb::SqliteDb(const std::string& path)
    : path_{path}
{
    init();
}

SqliteDb::~SqliteDb() {
    if (stmt_get_) sqlite3_finalize(stmt_get_);
    if (stmt_put_) sqlite3_finalize(stmt_put_);
    if (stmt_remove_) sqlite3_finalize(stmt_remove_);
    if (stmt_evict_expired_) sqlite3_finalize(stmt_evict_expired_);
    if (stmt_evict_oldest_) sqlite3_finalize(stmt_evict_oldest_);
    if (stmt_size_) sqlite3_finalize(stmt_size_);
    if (stmt_row_count_) sqlite3_finalize(stmt_row_count_);
    if (db_) sqlite3_close(db_);
}

void SqliteDb::init() {
    int rc = sqlite3_open_v2(path_.c_str(), &db_,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
                             nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Cannot open SQLite database: "} + sqlite3_errmsg(db_));
    }

    rc = sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to set WAL mode: "} + sqlite3_errmsg(db_));
    }

    rc = sqlite3_exec(db_,
        "CREATE TABLE IF NOT EXISTS cache ("
        "  key TEXT PRIMARY KEY,"
        "  content TEXT NOT NULL,"
        "  expires_at INTEGER NOT NULL"
        ");",
        nullptr, nullptr, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to create cache table: "} + sqlite3_errmsg(db_));
    }

    const char* sql_get = "SELECT content FROM cache WHERE key = ? AND expires_at > ?";
    rc = sqlite3_prepare_v2(db_, sql_get, -1, &stmt_get_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare get statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_put = "INSERT OR REPLACE INTO cache (key, content, expires_at) VALUES (?, ?, ?)";
    rc = sqlite3_prepare_v2(db_, sql_put, -1, &stmt_put_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare put statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_remove = "DELETE FROM cache WHERE key = ?";
    rc = sqlite3_prepare_v2(db_, sql_remove, -1, &stmt_remove_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare remove statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_evict_expired = "DELETE FROM cache WHERE expires_at <= ?";
    rc = sqlite3_prepare_v2(db_, sql_evict_expired, -1, &stmt_evict_expired_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare evict_expired statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_evict_oldest = "DELETE FROM cache WHERE rowid IN (SELECT rowid FROM cache ORDER BY expires_at ASC LIMIT ?)";
    rc = sqlite3_prepare_v2(db_, sql_evict_oldest, -1, &stmt_evict_oldest_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare evict_oldest statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_size = "SELECT page_count * page_size FROM pragma_page_count(), pragma_page_size()";
    rc = sqlite3_prepare_v2(db_, sql_size, -1, &stmt_size_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare size statement: "} + sqlite3_errmsg(db_));
    }

    const char* sql_row_count = "SELECT COUNT(*) FROM cache";
    rc = sqlite3_prepare_v2(db_, sql_row_count, -1, &stmt_row_count_, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error(std::string{"Failed to prepare row_count statement: "} + sqlite3_errmsg(db_));
    }
}

std::optional<std::string> SqliteDb::get(const std::string& key) {
    int64_t now = now_seconds();
    
    sqlite3_bind_text(stmt_get_, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt_get_, 2, now);

    int rc = sqlite3_step(stmt_get_);
    
    sqlite3_reset(stmt_get_);
    sqlite3_clear_bindings(stmt_get_);

    if (rc == SQLITE_ROW) {
        const char* content = reinterpret_cast<const char*>(sqlite3_column_text(stmt_get_, 0));
        if (content) {
            return std::string{content};
        }
    }

    return std::nullopt;
}

bool SqliteDb::put(const std::string& key, const std::string& value, int64_t ttl_seconds) {
    int64_t expires_at = now_seconds() + ttl_seconds;

    sqlite3_bind_text(stmt_put_, 1, key.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt_put_, 2, value.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_int64(stmt_put_, 3, expires_at);

    int rc = sqlite3_step(stmt_put_);
    
    sqlite3_reset(stmt_put_);
    sqlite3_clear_bindings(stmt_put_);

    return rc == SQLITE_OK || rc == SQLITE_DONE;
}

bool SqliteDb::remove(const std::string& key) {
    sqlite3_bind_text(stmt_remove_, 1, key.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt_remove_);
    
    sqlite3_reset(stmt_remove_);
    sqlite3_clear_bindings(stmt_remove_);

    return rc == SQLITE_OK || rc == SQLITE_DONE;
}

bool SqliteDb::evict_expired() {
    int64_t now = now_seconds();

    sqlite3_bind_int64(stmt_evict_expired_, 1, now);

    int rc = sqlite3_step(stmt_evict_expired_);
    
    sqlite3_reset(stmt_evict_expired_);
    sqlite3_clear_bindings(stmt_evict_expired_);

    return rc == SQLITE_OK || rc == SQLITE_DONE;
}

bool SqliteDb::evict_oldest(size_t count) {
    sqlite3_bind_int64(stmt_evict_oldest_, 1, static_cast<int64_t>(count));

    int rc = sqlite3_step(stmt_evict_oldest_);
    
    sqlite3_reset(stmt_evict_oldest_);
    sqlite3_clear_bindings(stmt_evict_oldest_);

    return rc == SQLITE_OK || rc == SQLITE_DONE;
}

void SqliteDb::checkpoint() {
    int pnLog = 0, pnCkpt = 0;
    sqlite3_wal_checkpoint_v2(db_, "main", SQLITE_CHECKPOINT_PASSIVE, &pnLog, &pnCkpt);
}

size_t SqliteDb::size_bytes() const {
    int rc = sqlite3_step(stmt_size_);
    
    sqlite3_reset(stmt_size_);

    if (rc == SQLITE_ROW) {
        return static_cast<size_t>(sqlite3_column_int64(stmt_size_, 0));
    }

    return 0;
}

size_t SqliteDb::row_count() const {
    int rc = sqlite3_step(stmt_row_count_);
    
    sqlite3_reset(stmt_row_count_);

    if (rc == SQLITE_ROW) {
        return static_cast<size_t>(sqlite3_column_int64(stmt_row_count_, 0));
    }

    return 0;
}

int64_t SqliteDb::now_seconds() const {
    return current_timestamp();
}

} // namespace cppreference::db
