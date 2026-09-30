#ifndef CPPREFERENCE_HTML_TO_MD_HPP
#define CPPREFERENCE_HTML_TO_MD_HPP

#include <string>

namespace cppreference::parser
{
    std::string convert_html_to_markdown(const std::string& html);

    std::string fix_urls(const std::string& input);
    std::string fix_cpp_spacing(const std::string& input);
    std::string fix_mathjax(const std::string& input);
} // namespace cppreference::parser

#endif // CPPREFERENCE_HTML_TO_MD_HPP
