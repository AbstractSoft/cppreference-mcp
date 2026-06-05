# cppreference-mcp

## Build

```bash
cmake -S . -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug
cmake --build cmake-build-debug
```

Release: replace `cmake-build-debug` with `cmake-build-release`.

Run: `out/Debug/cppreference-mcp` (binary + `config.json` copied alongside)

## Architecture

```
src/
├── main.cpp                  — entry point, config path resolution (macOS/Linux)
├── client/                   — cppreference.com HTTP client
│   ├── search()              — HTML search → regex extract /cpp/ links
│   ├── get_page_content()    — MediaWiki JSON API → revision content (unused)
│   └── get_page_html()       — direct HTML fetch for parser
├── server/                   — MCP JSON-RPC 2.0 server
│   ├── handle_initialize()   — protocol handshake
│   ├── handle_tools_list()   — advertise cppreference/lookup tool
│   ├── handle_tools_call()   — dispatch to thread pool with timeout
│   └── handle_tools_call_sync() — search → cache lookup → fetch → parse → truncate
├── parser/                   — HTML → markdown converter
│   ├── html_to_md.cpp/hpp    — cppreference::parser::convert_html_to_markdown()
│   │   — fix_urls(), fix_cpp_spacing(), noise filtering
├── config/                   — JSON config file loader
├── cache/                    — TTL-based SQLite cache wrapper (thread-safe)
│   ├── get()                 — mutex-locked, evict_expired every 50 calls
│   ├── put()                 — eviction before insert, row-count-based sizing
│   └── evict_expired()       — periodic TTL cleanup
├── db/                       — SQLite database layer (WAL mode)
│   ├── get()                 — filtered by expires_at > now
│   ├── put()                 — INSERT OR REPLACE with TTL
│   ├── evict_oldest()        — DELETE oldest rows by expires_at
│   ├── evict_expired()       — DELETE WHERE expires_at <= now
│   ├── checkpoint()          — PASSIVE WAL checkpoint
│   └── row_count()           — SELECT COUNT(*)
└── thread_pool/              — Async task queue (future-based)
    ├── submit()              — template method, returns std::future
    ├── wait_all()            — blocks until all tasks complete
    └── wait_all_with_timeout() — waits with deadline
```

Namespaces match folder structure: `cppreference::client`, `cppreference::server`, etc.

## Threading

- **HTTP client**: `std::mutex` protects shared `httplib::Client` during `Get()` calls. Client is a `unique_ptr` created once in the constructor.
- **Cache**: `std::mutex` serializes all `get`/`put`/`remove`/`evict_expired`/`size_bytes`/`row_count` calls. `get_call_count_` is `std::atomic`.
- **Server logging**: `static std::mutex log_mutex_` prevents interleaved output from concurrent thread pool workers.
- **Thread pool**: Worker threads process `handle_tools_call_sync` tasks with configurable timeout (5× client timeout).
- **SQLite**: Opened with `SQLITE_OPEN_NOMUTEX` — all access must be synchronized by the caller (enforced via `Cache` mutex).
- **html_md parser**: Thread-safe — concurrent `convert()` calls on the same `Converter` instance use per-call stack state (no mutable members).

## Parser Design

### Public API
- `convert_html_to_markdown(const std::string& html) → std::string` — single entry point, processes HTML → markdown

### html_md Library (FetchContent)
- Fetched from `https://github.com/AbstractSoft/html_md.git` via CMake FetchContent
- Powered by gumbo-parser (HTML5 parser, Apache 2.0)
- Thread-safe: concurrent `convert()` calls on the same instance
- Converts: headings, paragraphs, lists, links, images, code blocks, blockquotes, tables
- Custom tag handlers via `setTagHandler()`
- Built as `libhtml_md.a` static library, linked to cppreference-mcp

### gumbo-parser (FetchContent)
- Fetched from `https://codeberg.org/gumbo-parser/gumbo-parser.git` v0.13.2 via FetchContent
- C99 HTML5 parser, Apache 2.0
- Meson-based project — CMake wrapper generated at configure time to build as static library
- Handles HTML5 parsing with proper DOM tree construction

### Post-Processing (html_to_md.cpp)
- `fix_urls()` — rewrites `(/` relative URLs to full `https://www.cppreference.com` URLs
- `fix_cpp_spacing()` — inserts spaces after C++ keywords (`template`, `class`, `namespace`, `using`, `typedef`) when followed immediately by an alpha character
- Noise filtering: removes navigation headings, tool links, view links, action links, variant links, search links, "In other languages", categories, edit links, nav list items, navigation table rows
- Section extraction: finds first useful `###` section (Template parameters) to last useful section
- **Section trimming**: analyzes markdown from the end, identifies noise sections (See also, External links, References, Categories, Navigation, Tools, etc.) vs relevant sections (Example, Defect reports, Notes, Member functions, etc.), and removes trailing noise sections
- Blank line normalization: collapses 3+ consecutive blank lines to 2

### Key Design Decisions

### Search Strategy
- Uses HTML search page (`/index.php?title=Special:Search`) instead of broken MediaWiki JSON search API (which fails on `::` queries).
- Regex extracts `/cpp/...` hrefs from search results.
- Returns first `cpp/` path (e.g., `cpp/container/vector`).

### Page Content
- Fetches rendered HTML directly via `get_page_html(title)` — no wikitext processing needed.

### Cache Eviction
- **Before insert**: size check and eviction happen before `put()` to prevent exceeding `max_size_mb`.
- **Row-count-based**: `to_evict = (overage * 11) / (avg_row_size * 10)` — estimates average row size from current total size / row count, evicts enough rows to cover the overage with 10% headroom.
- **TTL expiration**: `evict_expired()` called automatically every 50 `get()` calls via atomic counter.

### HTTP Client
- `std::unique_ptr<httplib::Client>` — single instance for lifetime of `Client`.
- Browser User-Agent to avoid 403 responses from cppreference.com.
- Connection keep-alive (no per-request teardown).
- Retry loop with configurable delay on 429/503 and connection failures.

## Dependencies

Fetched at configure time via CMake FetchContent:
- `nlohmann/json` v3.11.3 (JSON)
- `yhirose/cpp-httplib` v0.16.0 (HTTP client, SSL-enabled)
- `AbstractSoft/thread_pool` (async task queue)
- `AbstractSoft/configuration` (JSON config loading, header-only)
- `AbstractSoft/html_md` (HTML-to-markdown converter, Apache 2.0)
- `gumbo-parser` 0.13.2 (HTML5 parser, Apache 2.0) — fetched by html_md

System:
- SQLite3
- OpenSSL (required by cpp-httplib)

## Configuration

`config.json` is copied to the output directory alongside the binary on every build.
Required sections: `cache`, `client`, `content`, `server`. All validated at startup.

## Tooling

- C++23 required (`#error` guard in source)
- clangd LSP configured via `opencode.json` with `--clang-tidy`
- `compile_commands.json` present (generated by CMake)
- Build artifacts live in `out/<Debug|Release>/` (gitignored)
