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
│   └── get_page_content()    — MediaWiki JSON API → revision content
├── server/                   — MCP JSON-RPC 2.0 server
│   ├── handle_initialize()   — protocol handshake
│   ├── handle_tools_list()   — advertise cppreference/lookup tool
│   ├── handle_tools_call()   — dispatch to thread pool with timeout
│   └── handle_tools_call_sync() — search → cache lookup → fetch → parse → truncate
├── parser/                   — Wikitext → minimal markdown converter
│   ├── convert()             — main entry point (public API)
│   └── [anonymous namespace] — parse_template_args, find_closing_braces, strip_templates, process_template, process_template_body, resolve_wiki_link, resolve_wiki_links_in_text, normalize_whitespace
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

## Parser Design

### Public API
- `convert(std::string_view wikitext) → std::string` — single entry point, processes wikitext → markdown

### Internal Functions (anonymous namespace)
- `parse_template_args()` — splits `{{name|arg1|arg2|named=val}}` args by top-level `|`, respecting `{{ }}` nesting
- `find_closing_braces()` — matches `}}` with depth tracking for nested `{{ }}`
- `strip_templates()` — removes `{{ }}` markers, keeps first positional arg (e.g., `{{named req|Container}}` → `Container`)
- `process_template()` — dispatches template body to markdown output (code blocks, inline code, noise)
- `process_template_body()` — processes `{{ }}` templates in prose/wiki-link display text
- `resolve_wiki_link()` — `[[target|display]]` → processed display text; `[[target]]` → leaf name
- `resolve_wiki_links_in_text()` — resolves `[[ ]]` in headings, respecting `{{ }}` nesting
- `normalize_whitespace()` — collapses multiple spaces/newlines, passes code fences verbatim

### Template Handling
- **Noise templates**: `par begin/end/inc`, `dsc begin/end/inc`, `ftm begin/end`, `cpp/navbar*`, `langlinks`, `todo`, `cpp/title` → dropped
- **Inline code**: `{{tt|...}}`, `{{lc|...}}`, `{{c/core|...}}`, `{{c|...}}`, `{{lcf|...}}`, `{{ltt|...}}` → `` `...` ``
- **Concepts**: `{{named req|...}}`, `{{lconcept|...}}` → plain text
- **Math**: `{{math|...}}` → plain text (Unicode preserved)
- **Declarations**: `{{dcl|...}}`, `{{ddcl|...}}` → fenced `` ```cpp `` blocks; `{{dcl header|...}}` → `// #include <...>`
- **Examples**: `{{example|...}}`, `{{cpp/example|...}}` → code block + output text
- **Source code**: `{{source|...}}` → fenced code block with language
- **Revision notes**: `{{rev inl|...}}`, `{{rrev|...}}` → last positional arg
- **Mark templates**: `{{mark|since=...}}`, `{{mark|until=...}}`, `{{mark|rev=...}}` → `*(since ...)*`
- **Constexpr**: `{{cpp/is_constexpr|since=c++11}}` → `(constexpr since c++11)`
- **Unknown templates**: silently dropped

### Stateful Block Processing
- **dsc blocks** (`{{dsc begin}}` → `{{dsc end}}`): Collect rows into `dsc_rows`, render as Markdown table on `{{dsc end}}`
  - `{{dsc hitem|...|...}}` → header row + separator marker
  - `{{dsc class|...|...}}`, `{{dsc function|...|...}}`, etc. → data rows
  - `{{dsc *}}` fallback → generic item row
- **par blocks** (`{{par begin}}` → `{{par end}}`): Collect items into `par_items`, render as bullet list on `{{par end}}`
  - `{{par|name|desc}}` → `- \`name\` — desc`

### Wiki Link Resolution
- Main loop processes `[[ ]]` before `{{ }}` to handle `[[link|{{c/core|bool}}]]` correctly
- Display text with templates is processed via `process_template_body()`
- Headings resolve wiki links via `resolve_wiki_links_in_text()` after template processing
- No display text → uses path leaf (`cpp/container/vector` → `vector`)

### Key Design Decisions

### Search Strategy
- Uses HTML search page (`/index.php?title=Special:Search`) instead of broken MediaWiki JSON search API (which fails on `::` queries).
- Regex extracts `/cpp/...` hrefs from search results.
- Returns first `cpp/` path (e.g., `cpp/container/vector`).

### Page Content
- Uses MediaWiki REST API (`/api.php?action=query&prop=revisions&rvprop=content&format=json`) which works reliably with `cpp/` title paths.

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
- System SQLite3 + OpenSSL (required by cpp-httplib)

## Configuration

`config.json` is copied to the output directory alongside the binary on every build.
Required sections: `cache`, `client`, `content`, `server`. All validated at startup.

## Tooling

- C++23 required (`#error` guard in source)
- clangd LSP configured via `opencode.json` with `--clang-tidy`
- `compile_commands.json` present (generated by CMake)
- Build artifacts live in `out/<Debug|Release>/` (gitignored)
