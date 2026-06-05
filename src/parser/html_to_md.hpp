#ifndef CPPREFERENCE_HTML_TO_MD_HPP
#define CPPREFERENCE_HTML_TO_MD_HPP

#include <string>

namespace cppreference::parser
{
    std::string convert_html_to_markdown(const std::string& html);
} // namespace cppreference::parser

#endif // CPPREFERENCE_HTML_TO_MD_HPP
