#include "internal/compiler.hpp"

#include <algorithm>

namespace cxxpython::core {
SourceBuffer::SourceBuffer(std::string text, std::string filename) : filename_(std::move(filename))
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        auto ch = text[i];
        if (ch == '\r') {
            if (i + 1 < text.size() && text[i + 1] == '\n')
                ++i;
            ch = '\n';
        }
        text_.push_back(ch);
        if (ch == '\n')
            lines_.push_back(text_.size());
    }
    (void)detail::decode_source(*this, {0, text_.size()});
}
SourcePosition SourceBuffer::position(Span span) const
{
    auto at = [&](std::size_t offset) {
        offset = std::min(offset, text_.size());
        const auto row = static_cast<std::size_t>(
            std::upper_bound(lines_.begin(), lines_.end(), offset) - lines_.begin() - 1);
        return std::pair{static_cast<std::uint32_t>(row + 1),
                         static_cast<std::uint32_t>(offset - lines_[row])};
    };
    const auto [line, column] = at(span.begin);
    const auto [end_line, end_column] = at(span.end);
    return {line, column, end_line, end_column};
}
std::string SourceBuffer::line(std::uint32_t number) const
{
    if (!number || number > lines_.size())
        return {};
    const auto start = lines_[number - 1];
    const auto end = text_.find('\n', start);
    return text_.substr(start, end == std::string::npos ? end : end - start + 1);
}
SourceError::SourceError(const SourceBuffer &source, Span where, std::string message,
                         SourceErrorKind category)
    : std::invalid_argument(std::move(message)), kind(category), span(where),
      position(source.position(where)), filename(source.filename()),
      source_line(source.line(position.line)),
      offset(source.character_column(position.line, position.column) + 1),
      end_offset(source.character_column(position.end_line, position.end_column) + 1)
{}
std::uint32_t SourceBuffer::character_column(std::uint32_t number, std::uint32_t byte_column) const
{
    const auto text = line(number);
    const auto end = text.begin() + std::min<std::size_t>(byte_column, text.size());
    return static_cast<std::uint32_t>(
        std::count_if(text.begin(), end, [](unsigned char byte) { return (byte & 0xc0) != 0x80; }));
}
std::string source_utf8(std::u32string_view text)
{
    std::string out;
    for (auto point : text) {
        if (point >= 0xd800 && point <= 0xdfff) {
            constexpr char hex[] = "0123456789abcdef";
            out += "\\u";
            for (int shift : {12, 8, 4, 0})
                out += hex[(point >> shift) & 15];
        } else if (point < 0x80)
            out += static_cast<char>(point);
        else if (point < 0x800) {
            out += static_cast<char>(0xc0 | (point >> 6));
            out += static_cast<char>(0x80 | (point & 63));
        } else if (point < 0x10000) {
            out += static_cast<char>(0xe0 | (point >> 12));
            out += static_cast<char>(0x80 | ((point >> 6) & 63));
            out += static_cast<char>(0x80 | (point & 63));
        } else if (point <= 0x10ffff) {
            out += static_cast<char>(0xf0 | (point >> 18));
            out += static_cast<char>(0x80 | ((point >> 12) & 63));
            out += static_cast<char>(0x80 | ((point >> 6) & 63));
            out += static_cast<char>(0x80 | (point & 63));
        } else
            throw std::invalid_argument("invalid Unicode codepoint");
    }
    return out;
}
} // namespace cxxpython::core

namespace cxxpython::core::detail {
static std::u32string decode_utf8(const SourceBuffer &source, std::string_view text, Span span)
{
    std::u32string out;
    for (auto i = span.begin; i < span.end;) {
        const auto start = i;
        const auto first = static_cast<unsigned char>(text[i++]);
        char32_t point;
        int extra;
        if (first < 0x80) {
            point = first;
            extra = 0;
        } else if (first >= 0xc2 && first <= 0xdf) {
            point = first & 31;
            extra = 1;
        } else if (first >= 0xe0 && first <= 0xef) {
            point = first & 15;
            extra = 2;
        } else if (first >= 0xf0 && first <= 0xf4) {
            point = first & 7;
            extra = 3;
        } else
            throw SourceError(source, {start, i}, "invalid UTF-8 source",
                              SourceErrorKind::Encoding);
        for (int n = 0; n < extra; ++n) {
            if (i == span.end || (static_cast<unsigned char>(text[i]) & 0xc0) != 0x80)
                throw SourceError(source, {start, i}, "invalid UTF-8 source",
                                  SourceErrorKind::Encoding);
            point = (point << 6) | (static_cast<unsigned char>(text[i++]) & 63);
        }
        if (!point || (extra == 1 && point < 0x80) || (extra == 2 && point < 0x800) ||
            (extra == 3 && point < 0x10000) || point > 0x10ffff ||
            (point >= 0xd800 && point <= 0xdfff))
            throw SourceError(source, {start, i}, "invalid UTF-8 source or null character",
                              SourceErrorKind::Encoding);
        out.push_back(point);
    }
    return out;
}
std::u32string decode_source(const SourceBuffer &source, Span span)
{
    return decode_utf8(source, source.text(), span);
}
std::u32string decode_filename(const SourceBuffer &source)
{
    return decode_utf8(source, source.filename(), {0, source.filename().size()});
}
} // namespace cxxpython::core::detail
