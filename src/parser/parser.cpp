#include "parser.hpp"
#include <algorithm>
#include <cctype>
#include <vector>

namespace cppreference::parser {

namespace {

struct TemplateArg {
    std::string name;
    std::string value;

    TemplateArg() = default;
    TemplateArg(std::string n, std::string val) : name(std::move(n)), value(std::move(val)) {}
};

// Parse top-level pipe-separated arguments of a template body (after the name|).
std::vector<TemplateArg> parse_template_args(std::string_view args) {
    std::vector<TemplateArg> result;
    std::size_t pos = 0;

    while (pos <= args.size()) {
        std::size_t depth = 0;
        std::size_t pipe_pos = std::string::npos;

        for (std::size_t idx = pos; idx < args.size(); ++idx) {
            if (args[idx] == '{' && idx + 1 < args.size() && args[idx + 1] == '{') { depth++; idx++; }
            else if (args[idx] == '}' && idx + 1 < args.size() && args[idx + 1] == '}') { depth--; idx++; }
            else if (args[idx] == '|' && depth == 0) { pipe_pos = idx; break; }
        }

        std::string_view arg_sv = pipe_pos == std::string::npos
            ? args.substr(pos)
            : args.substr(pos, pipe_pos - pos);

        // Named argument: first '=' that is not inside {{ }}
        std::size_t eq_pos = std::string::npos;
        std::size_t brace_depth = 0;
        for (std::size_t idx = 0; idx < arg_sv.size(); ++idx) {
            if (arg_sv[idx] == '{' && idx + 1 < arg_sv.size() && arg_sv[idx+1] == '{') { brace_depth++; idx++; }
            else if (arg_sv[idx] == '}' && idx + 1 < arg_sv.size() && arg_sv[idx+1] == '}') { brace_depth--; idx++; }
            else if (arg_sv[idx] == '=' && brace_depth == 0) { eq_pos = idx; break; }
        }

        if (eq_pos != std::string::npos) {
            result.push_back({ std::string(arg_sv.substr(0, eq_pos)),
                               std::string(arg_sv.substr(eq_pos + 1)) });
        } else {
            TemplateArg arg;
            arg.name = std::to_string(result.size());
            arg.value = std::string(arg_sv);
            result.push_back(arg);
        }

        if (pipe_pos == std::string::npos) {
            break;
        }
        pos = pipe_pos + 1;
    }

    return result;
}

// Find the position just past the }} that closes the {{ at text[0..1].
std::size_t find_closing_braces(std::string_view text) {
    std::size_t depth = 1;
    for (std::size_t idx = 2; idx + 1 < text.size(); ++idx) {
        if (text[idx] == '{' && text[idx + 1] == '{') { depth++; idx++; }
        else if (text[idx] == '}' && text[idx + 1] == '}') {
            if (--depth == 0) {
                return idx + 2;
            }
            idx++;
        }
    }
    return text.size();
}

// Strip all {{ }} template markers and keep only the first positional argument
std::string strip_templates(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::size_t pos = 0;

    while (pos < text.size()) {
        if (pos + 1 < text.size() && text[pos] == '{' && text[pos + 1] == '{') {
            std::size_t end = find_closing_braces(text.substr(pos));
            std::string_view inner = text.substr(pos + 2, end - 4);
            std::size_t pipe = inner.find('|');
            if (pipe != std::string::npos) {
                result += strip_templates(inner.substr(pipe + 1));
            }
            pos += end;
        } else {
            result += text[pos++];
        }
    }
    return result;
}

// Forward declarations
std::string process_template(std::string_view body, bool& in_dsc, std::vector<std::string>& dsc_rows, bool& in_par, std::vector<std::string>& par_items);
std::string process_template_body(std::string_view text, bool& in_dsc, std::vector<std::string>& dsc_rows, bool& in_par, std::vector<std::string>& par_items);
std::string resolve_wiki_link(std::string_view link);
std::string resolve_wiki_links_in_text(std::string_view text);

// Resolve a wiki link body to display text.
std::string resolve_wiki_link(std::string_view link) {
    std::size_t pipe = link.find('|');
    if (pipe != std::string::npos) {
        std::string display = std::string(link.substr(pipe + 1));
        bool local_dsc = false;
        bool local_par = false;
        std::vector<std::string> local_dsc_rows;
        std::vector<std::string> local_par_items;
        return process_template_body(display, local_dsc, local_dsc_rows, local_par, local_par_items);
    }
    std::size_t slash = link.rfind('/');
    return slash != std::string::npos ? std::string(link.substr(slash + 1)) : std::string(link);
}

// Process inline templates in prose text
std::string process_template_body(std::string_view text, bool& in_dsc, std::vector<std::string>& dsc_rows, bool& in_par, std::vector<std::string>& par_items) {
    std::string result;
    result.reserve(text.size());
    std::size_t pos = 0;

    while (pos < text.size()) {
        std::size_t next = std::string::npos;
        for (std::size_t idx = pos; idx + 1 < text.size(); ++idx) {
            if (text[idx] == '{' && text[idx + 1] == '{') { next = idx; break; }
        }

        if (next == std::string::npos) {
            result += text.substr(pos);
            break;
        }

        result += text.substr(pos, next - pos);

        std::string_view remaining = text.substr(next);
        std::size_t template_end = find_closing_braces(remaining);
        std::string_view body = remaining.substr(2, template_end - 4);

        result += process_template(body, in_dsc, dsc_rows, in_par, par_items);
        pos = next + template_end;
    }

    return result;
}

// Resolve wiki links in text
std::string resolve_wiki_links_in_text(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::size_t pos = 0;

    while (pos < text.size()) {
        std::size_t next = std::string::npos;
        for (std::size_t idx = pos; idx + 1 < text.size(); ++idx) {
            if (text[idx] == '[' && text[idx + 1] == '[') { next = idx; break; }
        }

        if (next == std::string::npos) {
            result += text.substr(pos);
            break;
        }

        result += text.substr(pos, next - pos);

        std::size_t close_pos = next + 2;
        std::size_t brace_depth = 0;
        while (close_pos + 1 < text.size()) {
            if (text[close_pos] == '{' && text[close_pos + 1] == '{') {
                brace_depth++;
                close_pos += 2;
            } else if (text[close_pos] == '}' && text[close_pos + 1] == '}') {
                brace_depth--;
                close_pos += 2;
            } else if (brace_depth == 0 && text[close_pos] == ']' && text[close_pos + 1] == ']') {
                break;
            } else {
                ++close_pos;
            }
        }

        std::string link_str = std::string(text.substr(next + 2, close_pos - (next + 2)));
        result += resolve_wiki_link(link_str);
        pos = close_pos + 2;
    }

    return result;
}

// Process a single template and return its markdown representation.
std::string process_template(std::string_view body, bool& in_dsc, std::vector<std::string>& dsc_rows, bool& in_par, std::vector<std::string>& par_items) {
    // Find template name: up to first top-level |
    std::size_t name_end = 0;
    std::size_t depth = 0;
    for (; name_end < body.size(); ++name_end) {
        if (body[name_end] == '{' && name_end + 1 < body.size() && body[name_end + 1] == '{') { depth++; name_end++; }
        else if (body[name_end] == '}' && name_end + 1 < body.size() && body[name_end + 1] == '}') { depth--; name_end++; }
        else if (body[name_end] == '|' && depth == 0) {
            break;
        }
    }

    std::string tname(body.substr(0, name_end));
    tname.erase(0, tname.find_first_not_of(" \t\n\r"));
    if (!tname.empty()) tname.erase(tname.find_last_not_of(" \t\n\r") + 1);

    std::string_view targs = name_end < body.size() ? body.substr(name_end + 1) : std::string_view{};

    // Noise: drop entirely
    if (tname == "par begin"   || tname == "par end"   || tname == "par inc"  ||
        tname == "dsc begin"   || tname == "dsc end"   || tname == "dsc inc"  ||
        tname == "ftm begin"   || tname == "ftm end"   || tname == "ftm"      ||
        tname == "dr list begin" || tname == "dr list item" || tname == "dr list end" ||
        tname == "langlinks"   || tname == "todo"      ||
        tname == "cpp/title"   ||
        (tname.size() > 4 && tname.substr(0, 4) == "cpp/" && tname.find("navbar") != std::string::npos)) {
        return "";
    }

    // dsc block handling
    if (tname == "dsc begin") {
        in_dsc = true;
        dsc_rows.clear();
        return "";
    }
    if (tname == "dsc end") {
        in_dsc = false;
        if (dsc_rows.empty()) {
            return "";
        }
        std::string table;
        bool has_header = false;
        if (!dsc_rows.empty()) {
            has_header = (dsc_rows[0].find("|---|") != std::string::npos);
        }
        if (has_header) {
            table += dsc_rows[0] + "\n";
            table += "|---|------|\n";
            for (std::size_t idx = 1; idx < dsc_rows.size(); ++idx) {
                table += dsc_rows[idx] + "\n";
            }
        } else {
            for (const auto& row : dsc_rows) {
                table += row + "\n";
            }
        }
        dsc_rows.clear();
        return table + "\n";
    }
    if (in_dsc) {
        if (tname == "dsc hitem") {
            auto args = parse_template_args(targs);
            std::string name, desc;
            for (const auto& arg : args) {
                if (arg.name == "1") name = strip_templates(arg.value);
                if (arg.name == "2") desc = strip_templates(arg.value);
            }
            dsc_rows.push_back("| " + name + " | " + desc + " |");
            dsc_rows.push_back("|---|------|");
            return "";
        }
        if (tname == "dsc class" || tname == "dsc struct" || tname == "dsc function" ||
            tname == "dsc member" || tname == "dsc typedef" || tname == "dsc enum" ||
            tname == "dsc concept") {
            auto args = parse_template_args(targs);
            std::string name, desc;
            for (const auto& arg : args) {
                if (arg.name == "1") name = strip_templates(arg.value);
                if (arg.name == "2") desc = strip_templates(arg.value);
            }
            dsc_rows.push_back("| " + name + " | " + desc + " |");
            return "";
        }
        if (tname.find("dsc") == 0) {
            if (targs.empty()) {
                return "";
            }
            auto args = parse_template_args(targs);
            if (!args.empty()) {
                std::string content = strip_templates(args[0].value);
                dsc_rows.push_back("| | " + content + " |");
            }
            return "";
        }
    }

    // par block handling
    if (tname == "par begin") {
        in_par = true;
        par_items.clear();
        return "";
    }
    if (tname == "par end") {
        in_par = false;
        if (par_items.empty()) {
            return "";
        }
        std::string list = "\n**Parameters:**\n";
        for (const auto& item : par_items) {
            list += "- " + item + "\n";
        }
        par_items.clear();
        return list + "\n";
    }
    if (in_par && tname == "par") {
        auto args = parse_template_args(targs);
        std::string name, desc;
        for (const auto& arg : args) {
            if (arg.name == "1") name = "`" + strip_templates(arg.value) + "`";
            if (arg.name == "2") desc = strip_templates(arg.value);
        }
        if (!name.empty() && !desc.empty()) {
            par_items.push_back(name + " \u2014 " + desc);
        }
        return "";
    }

    // Inline code templates
    if (tname == "tt" || tname == "lc" || tname == "c/core" || tname == "lcf" || tname == "c") {
        return "`" + strip_templates(targs) + "`";
    }
    if (tname == "ltt") {
        return "`" + strip_templates(targs) + "`";
    }

    // Named requirements / concepts
    if (tname == "named req" || tname == "lconcept") {
        return strip_templates(targs);
    }

    // Math
    if (tname == "math") {
        return strip_templates(targs);
    }

    // Revision inline
    if (tname == "rev inl" || tname == "rrev") {
        auto args = parse_template_args(targs);
        for (auto it = args.rbegin(); it != args.rend(); ++it) {
            bool is_positional = false;
            if (!it->name.empty()) {
                is_positional = std::all_of(it->name.begin(), it->name.end(), ::isdigit);
            }
            if (is_positional) {
                return strip_templates(it->value);
            }
        }
        return "";
    }

    // Mark templates
    if (tname == "mark" || tname == "mark rev") {
        auto args = parse_template_args(targs);
        std::string since, until, rev_text;
        for (const auto& arg : args) {
            if (arg.name == "since") since = arg.value;
            if (arg.name == "until") until = arg.value;
            if (arg.name == "rev")   rev_text = arg.value;
        }
        if (!since.empty()) return "*(since " + strip_templates(since) + ")* ";
        if (!until.empty()) return "*(until " + strip_templates(until) + ")* ";
        if (!rev_text.empty()) return "*(rev: " + strip_templates(rev_text) + ")* ";
        return "";
    }

    // Constexpr note
    if (tname == "cpp/is_constexpr") {
        auto args = parse_template_args(targs);
        std::string since;
        for (const auto& arg : args) {
            if (arg.name == "since") {
                since = arg.value;
            }
        }
        return since.empty() ? "(constexpr)" : "(constexpr since " + since + ")";
    }

    // Declaration blocks
    if (tname == "dcl begin" || tname == "dcl end") {
        return "";
    }
    if (tname == "dcl header") {
        std::string header = strip_templates(targs);
        return "// #include <" + header + ">\n";
    }
    if (tname == "dcl" || tname == "dcl rev" || tname == "ddcl") {
        auto args = parse_template_args(targs);
        std::string code;
        for (const auto& arg : args) {
            if (arg.name == "1") {
                code = arg.value;
                break;
            }
        }
        if (code.empty()) {
            return "";
        }
        while (!code.empty() && code.front() == '\n') {
            code.erase(code.begin());
        }
        while (!code.empty() && code.back() == '\n') {
            code.pop_back();
        }
        return "```cpp\n" + code + "\n```\n";
    }

    // Example
    if (tname == "example" || tname == "cpp/example") {
        auto args = parse_template_args(targs);
        std::string code, output;
        for (const auto& arg : args) {
            if (arg.name == "code")   code   = arg.value;
            if (arg.name == "output") output = arg.value;
        }
        std::string result;
        if (!code.empty()) {
            while (!code.empty() && code.front() == '\n') {
                code.erase(code.begin());
            }
            while (!code.empty() && code.back() == '\n') {
                code.pop_back();
            }
            result += "```cpp\n" + code + "\n```\n";
        }
        if (!output.empty()) {
            while (!output.empty() && output.front() == '\n') {
                output.erase(output.begin());
            }
            while (!output.empty() && output.back() == '\n') {
                output.pop_back();
            }
            if (!output.empty()) {
                result += "Output: `" + output + "`\n";
            }
        }
        return result;
    }

    // Source code inclusion
    if (tname == "source") {
        auto args = parse_template_args(targs);
        std::string lang = "cpp";
        std::string code;
        for (const auto& arg : args) {
            if (arg.name == "1") code = arg.value;
            if (arg.name == "lang") lang = arg.value;
        }
        if (!code.empty()) {
            while (!code.empty() && code.front() == '\n') {
                code.erase(code.begin());
            }
            while (!code.empty() && code.back() == '\n') {
                code.pop_back();
            }
            return "```" + lang + "\n" + code + "\n```\n";
        }
        return "";
    }

    // Unknown templates: silently drop
    return "";
}

// Collapse runs of blank lines to at most two newlines.
std::string normalize_whitespace(std::string_view text) {
    std::string result;
    result.reserve(text.size());
    std::size_t idx = 0;
    bool in_fence = false;

    while (idx < text.size()) {
        // Pass code fences through unchanged
        if (idx + 2 < text.size() && text[idx] == '`' && text[idx+1] == '`' && text[idx+2] == '`') {
            std::size_t fence_end = text.find("```", idx + 3);
            std::size_t copy_end = fence_end == std::string::npos ? text.size() : fence_end + 3;
            result += text.substr(idx, copy_end - idx);
            idx = copy_end;
            continue;
        }

        // Collapse 3+ consecutive newlines to 2
        if (text[idx] == '\n') {
            std::size_t nl_count = 0;
            while (idx < text.size() && text[idx] == '\n') {
                ++nl_count;
                ++idx;
            }
            result += (nl_count >= 2) ? "\n\n" : "\n";
            continue;
        }

        // Collapse multiple spaces into one
        if (text[idx] == ' ') {
            std::size_t sp_count = 0;
            while (idx < text.size() && text[idx] == ' ') {
                ++sp_count;
                ++idx;
            }
            result += ' ';
            continue;
        }

        result += text[idx++];
    }

    // Trim leading/trailing whitespace
    while (!result.empty() && (result.front() == '\n' || result.front() == ' ')) {
        result.erase(result.begin());
    }
    while (!result.empty() && (result.back() == '\n' || result.back() == ' ')) {
        result.pop_back();
    }

    return result;
}

} // namespace

std::string convert(std::string_view wikitext) {
    bool in_dsc = false;
    bool in_par = false;
    std::vector<std::string> dsc_rows;
    std::vector<std::string> par_items;

    std::string result;
    result.reserve(wikitext.size());
    std::size_t pos = 0;

    while (pos < wikitext.size()) {
        // Locate next marker
        std::size_t next_template = std::string::npos;
        for (std::size_t idx = pos; idx + 1 < wikitext.size(); ++idx) {
            if (wikitext[idx] == '{' && wikitext[idx + 1] == '{') { next_template = idx; break; }
        }

        std::size_t next_wiki = std::string::npos;
        for (std::size_t idx = pos; idx + 1 < wikitext.size(); ++idx) {
            if (wikitext[idx] == '[' && wikitext[idx + 1] == '[') { next_wiki = idx; break; }
        }

        std::size_t next_heading = wikitext.find("===", pos);
        std::size_t next_ref = wikitext.find("<ref", pos);

        std::size_t next = wikitext.size();
        if (next_template != std::string::npos) next = std::min(next, next_template);
        if (next_wiki     != std::string::npos) next = std::min(next, next_wiki);
        if (next_heading  != std::string::npos) next = std::min(next, next_heading);
        if (next_ref      != std::string::npos) next = std::min(next, next_ref);

        // Emit prose between markers
        if (next > pos) {
            std::string_view prose = wikitext.substr(pos, next - pos);

            std::string cleaned;
            cleaned.reserve(prose.size());
            for (std::size_t idx = 0; idx < prose.size(); ++idx) {
                if (prose[idx] == '@') {
                    ++idx;
                    while (idx < prose.size() && prose[idx] != '@') {
                        ++idx;
                    }
                    if (idx + 1 < prose.size() && prose[idx + 1] == ' ') {
                        ++idx;
                    }
                    continue;
                }
                cleaned += prose[idx];
            }

            result += cleaned;
        }

        if (next == wikitext.size()) {
            break;
        }

        // Handle <ref> / <references/>
        if (next_ref != std::string::npos && next == next_ref) {
            constexpr std::size_t refs_self_len = 13;
            constexpr std::size_t refs_space_len = 14;
            constexpr std::size_t ref_jump_no_space = 13;
            constexpr std::size_t ref_jump_with_space = 14;

            if (wikitext.size() > next_ref + 12 &&
                (wikitext.substr(next_ref, refs_self_len) == "<references/>" ||
                 wikitext.substr(next_ref, refs_space_len) == "<references />")) {
                pos = next_ref + (wikitext[next_ref + 12] == '>' ? ref_jump_no_space : ref_jump_with_space);
                continue;
            }
            std::size_t ref_close = wikitext.find("</ref>", next_ref);
            pos = ref_close == std::string::npos ? next_ref + 4 : ref_close + 6;
            continue;
        }

        // Handle [[wiki link]]
        if (next_wiki != std::string::npos && next == next_wiki) {
            std::size_t close_pos = next_wiki + 2;
            std::size_t brace_depth = 0;
            while (close_pos + 1 < wikitext.size()) {
                if (wikitext[close_pos] == '{' && wikitext[close_pos + 1] == '{') {
                    brace_depth++;
                    close_pos += 2;
                } else if (wikitext[close_pos] == '}' && wikitext[close_pos + 1] == '}') {
                    brace_depth--;
                    close_pos += 2;
                } else if (brace_depth == 0 && wikitext[close_pos] == ']' && wikitext[close_pos + 1] == ']') {
                    break;
                } else {
                    ++close_pos;
                }
            }
            std::string link_str = std::string(wikitext.substr(next_wiki + 2, close_pos - (next_wiki + 2)));
            result += resolve_wiki_link(link_str);
            pos = close_pos + 2;
            continue;
        }

        // Handle {{template}}
        if (next_template != std::string::npos && next == next_template) {
            std::string_view remaining = wikitext.substr(next_template);
            std::size_t template_end = find_closing_braces(remaining);
            std::string_view body = remaining.substr(2, template_end - 4);
            std::string processed = process_template(body, in_dsc, dsc_rows, in_par, par_items);
            result += processed;
            pos = next_template + template_end;
            continue;
        }

        // Handle === heading ===
        if (next_heading != std::string::npos && next == next_heading) {
            std::size_t level = 0;
            std::size_t eq_pos = next_heading;
            while (eq_pos < wikitext.size() && wikitext[eq_pos] == '=') {
                ++level;
                ++eq_pos;
            }

            std::size_t text_start = eq_pos;
            std::size_t close = wikitext.find(std::string(level, '='), text_start);
            std::size_t heading_end = close == std::string::npos ? wikitext.size() : close + level;

            std::string_view heading_text = close == std::string::npos
                ? wikitext.substr(text_start)
                : wikitext.substr(text_start, close - text_start);

            std::string ht(heading_text);
            ht.erase(0, ht.find_first_not_of(" \t\n\r"));
            if (!ht.empty()) ht.erase(ht.find_last_not_of(" \t\n\r") + 1);

            if (!ht.empty()) {
                ht = process_template_body(ht, in_dsc, dsc_rows, in_par, par_items);
                ht = resolve_wiki_links_in_text(ht);
                std::string hashes(std::max(std::size_t{2}, level - 1), '#');
                result += "\n" + hashes + " " + ht + "\n";
            }

            pos = heading_end;
            continue;
        }

        break;
    }

    return normalize_whitespace(result);
}

} // namespace cppreference::parser
