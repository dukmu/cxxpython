#include "cxxpython/parser.hpp"

#include <algorithm>
#include <cctype>
#include <string>
#include <utility>
#include <vector>

namespace cxxpython::syntax {
namespace {

#include "../Objects/unicode_identifiers.inc"
#include "unicode_normalization.inc"

template <std::size_t N>
bool in_ranges(const std::pair<char32_t, char32_t> (&ranges)[N], char32_t point)
{
    auto it =
        std::upper_bound(std::begin(ranges), std::end(ranges), point,
                         [](char32_t value, const auto &range) { return value < range.first; });
    return it != std::begin(ranges) && point <= (it - 1)->second;
}

unsigned combining_class(char32_t point)
{
    auto it = std::lower_bound(
        std::begin(normalization_combining), std::end(normalization_combining), point,
        [](const auto &entry, char32_t value) { return entry.point < value; });
    return it != std::end(normalization_combining) && it->point == point ? it->value : 0;
}

char32_t compose(char32_t first, char32_t second)
{
    constexpr char32_t sbase = 0xac00, lbase = 0x1100, vbase = 0x1161, tbase = 0x11a7;
    constexpr unsigned lcount = 19, vcount = 21, tcount = 28, ncount = vcount * tcount;
    if (first >= lbase && first < lbase + lcount && second >= vbase && second < vbase + vcount)
        return sbase + ((first - lbase) * vcount + second - vbase) * tcount;
    if (first >= sbase && first < sbase + lcount * ncount && (first - sbase) % tcount == 0 &&
        second > tbase && second < tbase + tcount)
        return first + second - tbase;
    auto key = std::make_pair(first, second);
    auto it = std::lower_bound(std::begin(normalization_compositions),
                               std::end(normalization_compositions), key,
                               [](const auto &entry, const auto &value) {
                                   return std::make_pair(entry.first, entry.second) < value;
                               });
    return it != std::end(normalization_compositions) && it->first == first && it->second == second
               ? it->result
               : 0;
}

std::u32string normalize(std::u32string_view input)
{
    std::u32string decomposed;
    for (char32_t point : input) {
        if (point >= 0xac00 && point <= 0xd7a3) {
            auto index = point - 0xac00;
            decomposed.push_back(0x1100 + index / 588);
            decomposed.push_back(0x1161 + (index % 588) / 28);
            if (index % 28)
                decomposed.push_back(0x11a7 + index % 28);
        } else {
            auto it = std::lower_bound(
                std::begin(normalization_decompositions), std::end(normalization_decompositions),
                point, [](const auto &entry, char32_t value) { return entry.point < value; });
            if (it != std::end(normalization_decompositions) && it->point == point)
                decomposed.append(normalization_decomposition_data + it->offset, it->length);
            else
                decomposed.push_back(point);
        }
    }
    for (std::size_t i = 1; i < decomposed.size(); ++i) {
        unsigned ccc = combining_class(decomposed[i]);
        if (!ccc)
            continue;
        std::size_t j = i;
        while (j && combining_class(decomposed[j - 1]) > ccc) {
            std::swap(decomposed[j], decomposed[j - 1]);
            --j;
        }
    }
    std::u32string result;
    std::size_t starter = 0;
    unsigned previous = 0;
    for (char32_t point : decomposed) {
        unsigned ccc = combining_class(point);
        char32_t composite = !result.empty() && (previous < ccc || previous == 0)
                                 ? compose(result[starter], point)
                                 : 0;
        if (composite) {
            result[starter] = composite;
        } else {
            if (!ccc)
                starter = result.size();
            result.push_back(point);
            previous = ccc;
        }
    }
    return result;
}

} // namespace
std::u32string normalize_identifier(std::u32string_view text)
{
    return normalize(text);
}
std::size_t identifier_length(std::u32string_view text)
{
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] != U'_' &&
            !(i ? in_ranges(xid_continue, text[i]) : in_ranges(xid_start, text[i])))
            return i;
    return text.size();
}
} // namespace cxxpython::syntax
