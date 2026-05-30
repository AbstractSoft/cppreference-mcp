# 2026-05-30 Caching, Threading, and Configuration

## Overview

Add SQLite-backed caching with configurable TTL, a thread pool for handling 50+ simultaneous connections, rate-limit handling, and a JSON configuration file to the cppreference-mcp server.

## Architecture

### Modules

| Module | Responsibility |
|--------|---------------|
| `Config` | Load/parse `config.json`, provide typed access to settings |
| `Cache` | SQLite-backed cache with TTL, size limits, thread-safe operations |
| `Client` | HTTP client for cppreference.com (existing, extended with rate-limit handling) |
| `ThreadPool` | Task queue for concurrent MCP requests (adapted from email_backup pattern) |
| `McpServer` | JSON-RPC 2.0 over stdio, dispatches to thread pool |

### Data Flow

```
stdin → McpServer → ThreadPool → Client → cppreference.com
                          ↓
                    Cache (read/write)
                          ↓
                   config.json
```

## Configuration (`config.json`)

```json
{
    "cache": {
        "enabled": true,
        "path": "./cache.db",
        "default_ttl_seconds": 300,
        "max_size_mb": 50
    },
    "client": {
        "timeout_seconds": 10,
        "rate_limit_retry_delay_ms": 1000,
        "max_retries": 3
    },
    "content": {
        "max_output_chars": 8000
    },
    "server": {
        "thread_pool_size": 10
    }
}
```

## Cache Design

### SQLite Schema

```sql
CREATE TABLE IF NOT EXISTS cache (
    key TEXT PRIMARY KEY,
    content TEXT NOT NULL,
    expires_at INTEGER NOT NULL
);
```

- `key`: URL-encoded query or page title
- `content`: Raw wiki markup from cppreference.com
- `expires_at`: Unix timestamp (seconds) when the entry becomes stale

### Operations

- **Read:** Query by key; if `expires_at > now()`, return content; otherwise treat as miss
- **Write:** INSERT or REPLACE with TTL computed from `default_ttl_seconds`
- **Eviction:** When DB size exceeds `max_size_mb`, DELETE oldest entries until under limit
- **Thread safety:** SQLite `MUTEX` mode + mutex wrapper for concurrent accesses

### TTL

- Configurable via `cache.default_ttl_seconds` (default: 300 = 5 minutes)
- Expired entries are silently ignored on read

## Rate Limit Handling

- Detect HTTP 429 responses from cppreference.com
- On 429: retry after `rate_limit_retry_delay_ms` with exponential backoff (up to `max_retries`)
- Cache miss on 429 after all retries → return error to MCP client
- Cache hit → serve from cache (respects TTL)

## Thread Pool

Adapted from email_backup's `ThreadPool` pattern:

- Configurable size via `server.thread_pool_size` (default: 10, supports 50+)
- Task queue with `std::queue<std::function<void()>>`
- Condition variables for worker wake-up
- Atomic `active_tasks_` counter (no mutex needed for reads)
- `wait_all()` with configurable timeout
- Non-copyable, non-movable (explicitly deleted)
- Workers loop: `cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); })`

## CMakeLists.txt Changes (from email_backup)

- Add `ENABLE_SANITIZERS` option (Debug builds only)
- Output binaries to `out/<Debug|Release>/` directory
- Copy `config.json` to output dir on build (POST_BUILD)
- Add Homebrew paths for macOS (`/opt/homebrew`), detect Linux paths
- Add SQLite3 dependency (`find_package(SQLite3 REQUIRED)` or pkg-config)
- Add thread pool source files to executable

## Removed 2000-Character Limit

The hardcoded truncation in `handle_tools_call` is replaced by the configurable `content.max_output_chars` (default: 8000).

## Files to Create

```
src/
├── main.cpp                    (unchanged, thin entry point)
├── CppReferenceClient.hpp      (existing, add rate-limit logic)
├── CppReferenceClient.cpp      (existing, add rate-limit logic)
├── McpServer.hpp               (existing, add ThreadPool, Cache deps)
├── McpServer.cpp               (existing, dispatch to thread pool)
├── Config.hpp                  (new)
├── Config.cpp                  (new)
├── Cache.hpp                   (new)
├── Cache.cpp                   (new)
├── ThreadPool.hpp              (new)
├── ThreadPool.cpp              (new)
config.json                     (new, copied to output dir)
```
