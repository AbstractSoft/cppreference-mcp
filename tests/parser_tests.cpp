#include "gtest/gtest.h"
#include "parser/html_to_md.hpp"

using namespace cppreference::parser;

// ── fix_urls ────────────────────────────────────────────────────────────────

TEST(ParserTest, FixUrls_RewritesRelativeLink) {
    auto result = fix_urls("[vector](</cpp/container/vector>)");
    EXPECT_EQ(result, "[vector](<https://www.cppreference.com/cpp/container/vector>)");
}

TEST(ParserTest, FixUrls_MultipleLinks) {
    auto result = fix_urls("[a](</cpp/a>) and [b](</cpp/b>)");
    EXPECT_EQ(result, "[a](<https://www.cppreference.com/cpp/a>) and [b](<https://www.cppreference.com/cpp/b>)");
}

TEST(ParserTest, FixUrls_AbsoluteUrlUntouched) {
    auto result = fix_urls("[ext](<https://example.com/page>)");
    EXPECT_EQ(result, "[ext](<https://example.com/page>)");
}

TEST(ParserTest, FixUrls_NoLinks) {
    auto result = fix_urls("plain text with no links");
    EXPECT_EQ(result, "plain text with no links");
}

TEST(ParserTest, FixUrls_LinkWithFragment) {
    auto result = fix_urls("[sec](</cpp/container/vector#Member_functions>)");
    EXPECT_EQ(result, "[sec](<https://www.cppreference.com/cpp/container/vector#Member_functions>)");
}

// ── fix_cpp_spacing ─────────────────────────────────────────────────────────

TEST(ParserTest, FixCppSpacing_InsertsSpaceBeforeUppercase) {
    auto result = fix_cpp_spacing("templateMyType");
    EXPECT_EQ(result, "template MyType");
}

TEST(ParserTest, FixCppSpacing_ClassKeyword) {
    auto result = fix_cpp_spacing("classMyType");
    EXPECT_EQ(result, "class MyType");
}

TEST(ParserTest, FixCppSpacing_DoesNotCorruptClassical) {
    auto result = fix_cpp_spacing("classical");
    EXPECT_EQ(result, "classical");
}

TEST(ParserTest, FixCppSpacing_DoesNotCorruptTemplates) {
    auto result = fix_cpp_spacing("templates");
    EXPECT_EQ(result, "templates");
}

TEST(ParserTest, FixCppSpacing_DoesNotCorruptClassification) {
    auto result = fix_cpp_spacing("classification");
    EXPECT_EQ(result, "classification");
}

TEST(ParserTest, FixCppSpacing_NamespaceKeyword) {
    auto result = fix_cpp_spacing("namespacestd");
    EXPECT_EQ(result, "namespace std");
}

TEST(ParserTest, FixCppSpacing_AlreadySpaced) {
    auto result = fix_cpp_spacing("template <class T>");
    EXPECT_EQ(result, "template <class T>");
}

TEST(ParserTest, FixCppSpacing_UsingKeyword) {
    auto result = fix_cpp_spacing("usingnamespace");
    EXPECT_EQ(result, "using namespace");
}

TEST(ParserTest, FixCppSpacing_TypedefKeyword) {
    auto result = fix_cpp_spacing("typedefint");
    EXPECT_EQ(result, "typedef int");
}

TEST(ParserTest, FixCppSpacing_KeywordAtStartOfLine) {
    auto result = fix_cpp_spacing("classFoo\nbar");
    EXPECT_EQ(result, "class Foo\nbar");
}

TEST(ParserTest, FixCppSpacing_KeywordAfterSpace) {
    auto result = fix_cpp_spacing("a classFoo b");
    EXPECT_EQ(result, "a class Foo b");
}

// ── fix_mathjax ─────────────────────────────────────────────────────────────

TEST(ParserTest, FixMathjax_RemovesScriptsizePattern) {
    // In actual markdown, LaTeX appears as \\(\\scriptsize N\\) (double backslashes)
    auto result = fix_mathjax("Given \\\\(\\\\scriptsize N\\\\)N as distance");
    EXPECT_EQ(result.find("scriptsize"), std::string::npos);
}

TEST(ParserTest, FixMathjax_PreservesRenderedText) {
    auto result = fix_mathjax("Given \\\\(\\\\scriptsize N\\\\)N as distance");
    EXPECT_NE(result.find("N"), std::string::npos);
}

TEST(ParserTest, FixMathjax_RemovesOComplexity) {
    auto result = fix_mathjax("\\\\(\\\\scriptsize O(N \\\\cdot \\\\log(N))\\\\)O(N·log(N))");
    EXPECT_EQ(result.find("scriptsize"), std::string::npos);
    EXPECT_NE(result.find("O(N·log(N))"), std::string::npos);
}

TEST(ParserTest, FixMathjax_NoLaTeX) {
    auto result = fix_mathjax("plain text without any latex");
    EXPECT_EQ(result, "plain text without any latex");
}

// ── convert_html_to_markdown (edge cases) ───────────────────────────────────

TEST(ParserTest, Convert_EmptyHtml) {
    auto result = convert_html_to_markdown("");
    EXPECT_TRUE(result.empty());
}

TEST(ParserTest, Convert_NoSections) {
    const char* html = R"(<html><body><p>Just a paragraph</p></body></html>)";
    auto result = convert_html_to_markdown(html);
    EXPECT_TRUE(result.empty());
}

TEST(ParserTest, Convert_OnlyTitle) {
    const char* html = R"(<html><body><h1>std::test</h1></body></html>)";
    auto result = convert_html_to_markdown(html);
    EXPECT_TRUE(result.empty());
}
