#include "html_to_md.hpp"
#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

#include "converter.hpp"

namespace cppreference::parser
{
    // Noise section headings (from end of page)
    static const std::vector<std::string> noise_section_keywords = {
        "See also",
        "External links",
        "References",
        "Categories",
        "Navigation",
        "Tools",
        "Views",
        "Actions",
        "Variants",
        "Search",
        "In other languages",
        "cppreference.com",
        "Namespaces",
        "Headers",
        "Macro definitions",
        "Utilities"
    };

    // Relevant section headings (from end of page)
    static const std::vector<std::string> relevant_section_keywords = {
        "Example",
        "Defect reports",
        "Notes",
        "Template parameters",
        "Specializations",
        "Member functions",
        "Non-member functions",
        "Member types",
        "Non-member functions",
        "Helper types",
        "Template parameters",
        "Possible implementation",
        "Complexity",
        "Exceptions",
        "Constants",
        "Object lifetime",
        "Standard library",
        "Deduction guides",
        "Iterator invalidation",
        "Nested types",
        "Data members",
        "Storage",
        "Aliases",
        "Type requirements"
    };

    struct Section
    {
        size_t start_line;
        size_t end_line;
        std::string heading;
        bool is_noise;
    };

    static std::string trim_string(const std::string& str)
    {
        auto start = str.find_first_not_of(" \t\n\r\xc2\xa0");
        if (start == std::string::npos) { return {}; }
        auto end = str.find_last_not_of(" \t\n\r\xc2\xa0");
        return str.substr(start, end - start + 1);
    }

    static std::string to_lower(const std::string& str)
    {
        std::string result = str;
        std::transform(result.begin(), result.end(), result.begin(),
                      [](unsigned char c) { return std::tolower(c); });
        return result;
    }

    static bool is_noise_section(const std::string& heading)
    {
        std::string lower_heading = to_lower(heading);

        for (const auto& keyword : noise_section_keywords)
        {
            if (lower_heading.find(to_lower(keyword)) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    static bool is_relevant_section(const std::string& heading)
    {
        std::string lower_heading = to_lower(heading);

        for (const auto& keyword : relevant_section_keywords)
        {
            if (lower_heading.find(to_lower(keyword)) != std::string::npos)
            {
                return true;
            }
        }
        return false;
    }

    std::string fix_cpp_spacing(const std::string& input)
    {
        std::string result = input;
        auto keywords = std::vector<std::string>{"template", "class", "namespace", "using", "typedef"};
        for (const auto& kw : keywords)
        {
            auto kw_len = kw.size();
            size_t pos = 0;
            while ((pos = result.find(kw, pos)) != std::string::npos)
            {
                // Word boundary: char before keyword must be non-alphanumeric (or start of string)
                bool valid_start = (pos == 0) ||
                    (!std::isalnum(static_cast<unsigned char>(result[pos - 1])));
                // Char after keyword must be alpha (the case we want to fix)
                bool needs_space = (pos + kw_len < result.size()) &&
                    std::isalpha(static_cast<unsigned char>(result[pos + kw_len]));

                if (valid_start && needs_space)
                {
                    result.insert(pos + kw_len, " ");
                    pos += kw_len + 1;
                }
                else
                {
                    pos += kw_len;
                }
            }
        }
        return result;
    }

    std::string fix_urls(const std::string& input)
    {
        std::string result = input;
        size_t pos = 0;
        while ((pos = result.find("(<", pos)) != std::string::npos)
        {
            if (pos + 2 < result.size() && result[pos + 2] == '/')
            {
                result.replace(pos, 2, "(<https://www.cppreference.com");
                pos += 31;
            }
            else
            {
                pos += 2;
            }
        }
        return result;
    }

    std::string fix_mathjax(const std::string& input)
    {
        std::string result = input;

        // Split into lines for processing
        std::vector<std::string> lines;
        std::istringstream stream(result);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line);
        }

        // Remove lines that contain only escaped LaTeX (\\(\\scriptsize ...\\))
        std::vector<std::string> cleaned_lines;
        for (const auto& l : lines)
        {
            std::string trimmed = trim_string(l);
            // Skip lines that are ONLY escaped LaTeX
            if (trimmed.find("\\\\(\\\\scriptsize") != std::string::npos &&
                trimmed.find("\\\\)") != std::string::npos &&
                trimmed.find(" ") == std::string::npos)
            {
                continue;
            }
            // Skip lines that are ONLY escaped LaTeX with spaces
            if (trimmed.find("\\\\(\\\\scriptsize") != std::string::npos &&
                trimmed.find("\\\\)") != std::string::npos)
            {
                // Check if line is mostly LaTeX
                size_t latex_count = 0;
                size_t total_chars = 0;
                for (char c : trimmed)
                {
                    if (c == '\\' || c == '(' || c == ')' || c == 's' || c == 'c' || c == 'r' ||
                        c == 'i' || c == 'p' || c == 't' || c == 'z' || c == 'e' || c == ' ' ||
                        c == 'O' || c == 'N' || c == '^' || c == '.' || c == 'l' || c == 'o' ||
                        c == '-' || c == '2' || c == '1' || c == '3' || c == '4' || c == '5' ||
                        c == '6' || c == '7' || c == '8' || c == '9' || c == '0' || c == 't')
                    {
                        latex_count++;
                    }
                    else if (c != ' ')
                    {
                        total_chars++;
                    }
                }
                if (latex_count > 0 && total_chars == 0) { continue; }
            }
            cleaned_lines.push_back(l);
        }

        // Now fix inline LaTeX patterns
        std::vector<std::string> final_lines;
        for (const auto& l : cleaned_lines)
        {
            std::string line = l;
            // Remove \\(\\scriptsize ...\\) patterns but keep text immediately after \\)
            size_t pos = 0;
            while ((pos = line.find("\\\\(\\\\scriptsize", pos)) != std::string::npos)
            {
                size_t end = line.find("\\\\)", pos + 2);
                if (end != std::string::npos)
                {
                    // Check if there's text immediately after \\)
                    size_t after_end = end + 3;
                    if (after_end < line.size() && line[after_end] != ' ' && line[after_end] != '\n')
                    {
                        // Keep the text after \\), only remove the LaTeX
                        line.erase(pos, end - pos + 3);
                        pos = end - 3;
                    }
                    else
                    {
                        line.erase(pos, end - pos + 4);
                    }
                }
                else
                {
                    pos += 2;
                }
            }
            // Remove standalone \\( and \\)
            while ((pos = line.find("\\\\(")) != std::string::npos)
            {
                line.erase(pos, 3);
            }
            while ((pos = line.find("\\\\)")) != std::string::npos)
            {
                line.erase(pos, 3);
            }
            final_lines.push_back(line);
        }

        // Join lines that are split due to LaTeX removal (e.g., "O(N2" + ")")
        std::vector<std::string> joined_lines;
        for (size_t i = 0; i < final_lines.size(); i++)
        {
            std::string line = final_lines[i];
            std::string trimmed = trim_string(line);
            
            // If line ends with incomplete expression (digit, letter, operator) and next line starts with closing paren
            if (i + 1 < final_lines.size() && !trimmed.empty())
            {
                char last_char = trimmed.back();
                std::string next_trimmed = trim_string(final_lines[i + 1]);
                if ((std::isdigit(static_cast<unsigned char>(last_char)) || 
                     std::isalpha(static_cast<unsigned char>(last_char)) ||
                     last_char == '^' || last_char == '(' || last_char == '.') &&
                    !next_trimmed.empty() && next_trimmed[0] == ')')
                {
                    // Join this line with the next
                    line += next_trimmed;
                    i++; // Skip next line
                }
            }
            joined_lines.push_back(line);
        }
        final_lines = joined_lines;

        // Rejoin
        std::string output;
        for (size_t i = 0; i < final_lines.size(); i++)
        {
            output += final_lines[i];
            if (i < final_lines.size() - 1) { output += "\n"; }
        }

        return output;
    }

    static bool is_nav_heading(const std::string& line)
    {
        static const char* nav_headings[] = {
            "##### Navigation", "##### Tools", "##### Views",
            "##### Actions", "##### Variants", "##### Search"
        };
        for (const auto* h : nav_headings)
        {
            if (line.starts_with(h)) { return true; }
        }
        return false;
    }

    static bool is_nav_list_item(const std::string& line)
    {
        if (line.size() < 3 || line[0] != '-' || line[1] != ' ') { return false; }
        static const char* nav_items[] = {
            "Support us", "Recent changes", "FAQ", "Offline version",
            "What links here", "Related changes", "Upload file", "Special pages",
            "Printable version", "Permanent link", "Page information",
            "Create account", "Log in", "Page", "Discussion",
            "Read", "View source", "View history"
        };
        for (const auto* item : nav_items)
        {
            auto item_len = strlen(item);
            // Direct match: "- Support us"
            if (line.compare(2, item_len, item) == 0) { return true; }
            // Markdown link: "- [Support us](<...>)"
            if (line.size() >= 2 + item_len + 2 && line[2] == '[' &&
                line.compare(3, item_len, item) == 0 && line[3 + item_len] == ']') { return true; }
        }
        return false;
    }

    static bool is_nav_table_row(const std::string& line)
    {
        if (line.size() < 4 || line[0] != '|' || line[1] != ' ') { return false; }
        auto bracket = line.find("[", 2);
        if (bracket == std::string::npos) { return false; }
        auto paren = line.find("(", bracket);
        if (paren == std::string::npos) { return false; }
        std::string text = line.substr(bracket + 1, paren - bracket - 1);
        return text == "cppreference.com" ||
               text.find("cpp/") == 0 ||
               text.find("Cppreference:") == 0 ||
               text == "Containers library" || text == "Sequence";
    }

    static bool is_noise_line(const std::string& line)
    {
        if (line.find("In other languages") != std::string::npos) { return true; }
        if (line.find("[Categories]") != std::string::npos) { return true; }
        if (line.find("[[edit]]") != std::string::npos) { return true; }
        if (line.find("From cppreference.com") != std::string::npos) { return true; }
        if (line.find("[]()#") != std::string::npos) { return true; }
        if (line == "#") { return true; }

        // Category links: - [Category:...] or - [Pages using...]
        if (line.find("[Category:") != std::string::npos) { return true; }
        if (line.find("[Pages using deprecated") != std::string::npos) { return true; }

        // Language links: - [English](...), - [Deutsch](...), etc.
        if (line.size() >= 4 && line[0] == '-' && line[1] == ' ' &&
            line[2] == '[' && line.find("cppreference.com/cpp/") != std::string::npos) { return true; }

        // Orphaned heading markers without text: #####
        if (line == "#####") { return true; }

        // Orphaned punctuation on its own line
        std::string trimmed = trim_string(line);
        if (trimmed == ":" || trimmed == ";" || trimmed == ",") { return true; }

        return false;
    }

    static bool is_section_heading(const std::string& line)
    {
        if (line.size() >= 6 && line.substr(0, 4) == "### ") { return true; }
        if (line.size() >= 7 && line.substr(0, 6) == "###### ") { return true; }
        if (line.size() >= 7 && line.substr(0, 6) == "##### ") { return true; }
        return false;
    }

    static std::string extract_heading_text(const std::string& line)
    {
        if (line.size() >= 7 && line.substr(0, 6) == "###### ") { return line.substr(6); }
        if (line.size() >= 7 && line.substr(0, 6) == "##### ") { return line.substr(6); }
        if (line.size() >= 6 && line.substr(0, 4) == "### ") { return line.substr(4); }
        return line;
    }

    static std::vector<Section> parse_sections(const std::vector<std::string>& lines)
    {
        std::vector<Section> sections;

        for (size_t i = 0; i < lines.size(); i++)
        {
            if (is_section_heading(lines[i]))
            {
                Section sec;
                sec.start_line = i;
                sec.heading = trim_string(extract_heading_text(lines[i]));
                sec.is_noise = is_noise_section(sec.heading);

                // Find end of this section (next heading or end of file)
                size_t j = i + 1;
                while (j < lines.size())
                {
                    if (is_section_heading(lines[j]))
                    {
                        break;
                    }
                    j++;
                }
                sec.end_line = j - 1;
                sections.push_back(sec);
                i = j - 1;
            }
        }

        return sections;
    }

    static bool has_relevant_content(const std::vector<std::string>& section_lines)
    {
        for (const auto& line : section_lines)
        {
            std::string trimmed = trim_string(line);
            if (trimmed.empty()) { continue; }
            // Skip section headings (### or ####)
            if (trimmed.size() >= 3 && trimmed[0] == '#' && trimmed[1] == '#') { continue; }
            // Skip category-like links
            if (trimmed.find("[Category:") != std::string::npos) { continue; }
            if (trimmed.find("[Pages using deprecated") != std::string::npos) { continue; }
            // Skip language links
            if (trimmed.size() >= 4 && trimmed[0] == '-' && trimmed[1] == ' ' &&
                trimmed[2] == '[' && trimmed.find("cppreference.com/cpp/") != std::string::npos) { continue; }
            if (trimmed.find("[[edit]]") != std::string::npos) { continue; }
            // Table rows, list items, code blocks, blockquotes are all relevant content
            return true;
        }
        return false;
    }

    static std::string trim_trailing_noise(const std::string& markdown)
    {
        // Split into lines
        std::vector<std::string> lines;
        std::istringstream stream(markdown);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line);
        }

        if (lines.empty()) { return markdown; }

        // Find and preserve the title line
        std::string title_line;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].size() >= 2 && lines[i][0] == '#' && lines[i][1] != '#')
            {
                title_line = lines[i];
                break;
            }
        }

        // Parse sections
        std::vector<Section> sections = parse_sections(lines);

        if (sections.empty()) { return markdown; }

        // Find the last relevant section from the end (with actual content)
        int last_relevant_idx = -1;
        for (int i = static_cast<int>(sections.size()) - 1; i >= 0; i--)
        {
            const auto& sec = sections[static_cast<size_t>(i)];
            if (is_noise_section(sec.heading)) { continue; }

            // Collect section lines to check for relevant content
            std::vector<std::string> sec_lines;
            for (size_t j = sec.start_line; j <= sec.end_line && j < lines.size(); j++)
            {
                if (!is_nav_heading(lines[j]) && !is_noise_line(lines[j]) &&
                    !is_nav_list_item(lines[j]) && !is_nav_table_row(lines[j]))
                {
                    sec_lines.push_back(lines[j]);
                }
            }

            if (has_relevant_content(sec_lines))
            {
                last_relevant_idx = i;
                break;
            }
        }

        // If all sections are noise or empty, return original
        if (last_relevant_idx < 0) { return markdown; }

        // Find the end line of the last relevant section
        size_t cutoff_line = sections[static_cast<size_t>(last_relevant_idx)].end_line;

        // Collect lines: title + relevant sections up to cutoff
        std::string result;
        if (!title_line.empty())
        {
            result += title_line + "\n\n";
        }

        for (size_t i = 0; i <= static_cast<size_t>(last_relevant_idx); i++)
        {
            const auto& sec = sections[i];
            if (is_noise_section(sec.heading)) { continue; }

            // Only include lines up to the cutoff
            size_t end = std::min(sec.end_line, cutoff_line);
            if (end >= lines.size()) { end = lines.size() - 1; }

            for (size_t j = sec.start_line; j <= end && j < lines.size(); j++)
            {
                // Filter noise lines
                if (is_nav_heading(lines[j])) { continue; }
                if (is_noise_line(lines[j])) { continue; }
                if (is_nav_list_item(lines[j])) { continue; }
                if (is_nav_table_row(lines[j])) { continue; }

                result += lines[j] + "\n";
            }
            result += "\n";
        }

        return result;
    }

    std::string convert_html_to_markdown(const std::string& html)
    {
        html_md::Options opts;
        std::string markdown = html_md::htmlToMarkdown(html, opts);

        // Fix URLs
        markdown = fix_urls(markdown);

        // Fix MathJax/LaTeX patterns
        markdown = fix_mathjax(markdown);

        // Split into lines
        std::vector<std::string> lines;
        std::istringstream stream(markdown);
        std::string line;
        while (std::getline(stream, line))
        {
            lines.push_back(line);
        }

        // Find all ### section headings
        std::vector<size_t> section_indices;
        for (size_t i = 0; i < lines.size(); i++)
        {
            if (lines[i].size() >= 6 && lines[i].substr(0, 4) == "### ")
            {
                section_indices.push_back(i);
            }
        }

        if (section_indices.empty())
        {
            return "";
        }

        // Find the title from the first # heading
        std::string title = "cppreference page";
        for (const auto& l : lines)
        {
            if (l.size() >= 2 && l[0] == '#' && l[1] != '#')
            {
                title = l.substr(2);
                break;
            }
        }

        // Find the first useful section (Template parameters)
        size_t first_section = 0;
        for (size_t idx : section_indices)
        {
            if (lines[idx].find("Template parameters") != std::string::npos)
            {
                first_section = idx;
                break;
            }
        }

        // Extract content from first useful section to end
        std::ostringstream output;
        output << "# " << title << "\n\n";

        for (size_t i = first_section; i < lines.size(); i++)
        {
            const auto& cur_line = lines[i];

            // Skip noise lines
            if (is_nav_heading(cur_line)) { continue; }
            if (is_noise_line(cur_line)) { continue; }
            if (is_nav_list_item(cur_line)) { continue; }
            if (is_nav_table_row(cur_line)) { continue; }

            output << cur_line << "\n";
        }

        markdown = output.str();

        // Trim trailing noise sections (See also, External links, References, Categories)
        markdown = trim_trailing_noise(markdown);

        // Fix C++ spacing
        markdown = fix_cpp_spacing(markdown);

        // Clean up extra blank lines (3+ -> 2)
        size_t pos = 0;
        while ((pos = markdown.find("\n\n\n", pos)) != std::string::npos)
        {
            markdown.replace(pos, 2, "\n\n");
            pos += 2;
        }

        // Remove whitespace-only lines (spaces/tabs/nb-spaces) but keep truly empty lines
        {
            std::vector<std::string> lines;
            std::istringstream stream(markdown);
            std::string line;
            while (std::getline(stream, line))
            {
                if (!line.empty() && trim_string(line).empty()) { continue; }
                lines.push_back(line);
            }
            markdown.clear();
            for (size_t i = 0; i < lines.size(); i++)
            {
                markdown += lines[i];
                if (i < lines.size() - 1) { markdown += "\n"; }
            }
        }

        // Trim trailing whitespace
        while (!markdown.empty() && (markdown.back() == ' ' || markdown.back() == '\n' || markdown.back() == '\r'))
        {
            markdown.pop_back();
        }

        return markdown;
    }
} // namespace cppreference::parser
