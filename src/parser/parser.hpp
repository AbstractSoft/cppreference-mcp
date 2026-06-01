#ifndef CPPREFERENCE_PARSER_HPP
#define CPPREFERENCE_PARSER_HPP

#include <string>
#include <string_view>

namespace cppreference::parser {

std::string convert(std::string_view wikitext, std::string_view base_url = "");

} // namespace cppreference::parser

#endif // CPPREFERENCE_PARSER_HPP
