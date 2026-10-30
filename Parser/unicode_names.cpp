#include "cxxpython/parser.hpp"

#include <array>
#include <cstdio>
#include <optional>
#include <string>

// Adapted from CPython 3.12.14 Modules/unicodedata.c and its generated name DB.
// See doc/licenses/python-3.12.14.txt. Removed Python/UCD-3.2 state and expose
// only single-character Unicode escapes, including aliases, not named sequences.
namespace cxxpython::syntax {
namespace {
using Py_UCS2 = char16_t;
#include "unicode_names_db.h"

char upper(char ch)
{
    return ch >= 'a' && ch <= 'z' ? ch - ('a' - 'A') : ch;
}
bool ideograph(char32_t code)
{
    constexpr std::pair<char32_t, char32_t> ranges[] = {
        {0x3400, 0x4dbf},   {0x4e00, 0x9fff},   {0x20000, 0x2a6df},
        {0x2a700, 0x2b739}, {0x2b740, 0x2b81d}, {0x2b820, 0x2cea1},
        {0x2ceb0, 0x2ebe0}, {0x30000, 0x3134a}, {0x31350, 0x323af}};
    for (auto [begin, end] : ranges)
        if (code >= begin && code <= end)
            return true;
    return false;
}
bool matches(unsigned code, std::string_view name)
{
    auto offset = phrasebook_offset1[code >> phrasebook_shift];
    std::size_t at =
        phrasebook_offset2[(offset << phrasebook_shift) + (code & ((1 << phrasebook_shift) - 1))];
    if (!at)
        return false;
    std::size_t index = 0;
    bool first = true;
    for (;;) {
        unsigned word = phrasebook[at++];
        if (word >= phrasebook_short)
            word = ((word - phrasebook_short) << 8) + phrasebook[at++];
        if (!first) {
            if (index >= name.size() || name[index++] != ' ')
                return false;
        }
        first = false;
        const auto *text = lexicon + lexicon_offset[word];
        while (*text < 128) {
            if (index >= name.size() || upper(name[index++]) != *text++)
                return false;
        }
        if (*text == 128)
            return index == name.size();
        if (index >= name.size() || upper(name[index++]) != (*text & 127))
            return false;
    }
}
std::optional<char32_t> hangul(std::string_view text)
{
    constexpr std::string_view parts[][3] = {
        {"G", "A", ""},    {"GG", "AE", "G"}, {"N", "YA", "GG"},   {"D", "YAE", "GS"},
        {"DD", "EO", "N"}, {"R", "E", "NJ"},  {"M", "YEO", "NH"},  {"B", "YE", "D"},
        {"BB", "O", "L"},  {"S", "WA", "LG"}, {"SS", "WAE", "LM"}, {"", "OE", "LB"},
        {"J", "YO", "LS"}, {"JJ", "U", "LT"}, {"C", "WEO", "LP"},  {"K", "WE", "LH"},
        {"T", "WI", "M"},  {"P", "YU", "B"},  {"H", "EU", "BS"},   {"", "YI", "S"},
        {"", "I", "SS"},   {"", "", "NG"},    {"", "", "J"},       {"", "", "C"},
        {"", "", "K"},     {"", "", "T"},     {"", "", "P"},       {"", "", "H"}};
    std::array<int, 3> chosen{-1, -1, -1};
    constexpr int counts[]{19, 21, 28};
    for (int column = 0; column < 3; ++column) {
        int longest = -1;
        for (int i = 0; i < counts[column]; ++i) {
            const auto part = parts[i][column];
            if (text.starts_with(part) && static_cast<int>(part.size()) > longest) {
                longest = static_cast<int>(part.size());
                chosen[column] = i;
            }
        }
        if (longest < 0)
            return {};
        text.remove_prefix(static_cast<std::size_t>(longest));
    }
    if (!text.empty())
        return {};
    return static_cast<char32_t>(0xac00 + (chosen[0] * 21 + chosen[1]) * 28 + chosen[2]);
}
} // namespace
std::optional<std::string> unicode_character_name(char32_t code)
{
    if (code > 0x10ffff)
        return {};
    if (code >= 0xac00 && code <= 0xd7a3) {
        constexpr std::string_view leading[] = {"G", "GG", "N", "D",  "DD", "R", "M",
                                                "B", "BB", "S", "SS", "",   "J", "JJ",
                                                "C", "K",  "T", "P",  "H"};
        constexpr std::string_view vowel[] = {"A",   "AE", "YA", "YAE", "EO", "E",  "YEO",
                                              "YE",  "O",  "WA", "WAE", "OE", "YO", "U",
                                              "WEO", "WE", "WI", "YU",  "EU", "YI", "I"};
        constexpr std::string_view trailing[] = {
            "",   "G",  "GG", "GS", "N",  "NJ", "NH", "D",  "L", "LG", "LM", "LB", "LS", "LT",
            "LP", "LH", "M",  "B",  "BS", "S",  "SS", "NG", "J", "C",  "K",  "T",  "P",  "H"};
        const auto syllable = code - 0xac00;
        return std::string("HANGUL SYLLABLE ") + std::string(leading[syllable / (21 * 28)]) +
               std::string(vowel[(syllable / 28) % 21]) + std::string(trailing[syllable % 28]);
    }
    if (ideograph(code)) {
        char hex[9];
        std::snprintf(hex, sizeof(hex), "%04X", static_cast<unsigned>(code));
        return std::string("CJK UNIFIED IDEOGRAPH-") + hex;
    }
    auto offset = phrasebook_offset1[code >> phrasebook_shift];
    std::size_t at =
        phrasebook_offset2[(offset << phrasebook_shift) + (code & ((1 << phrasebook_shift) - 1))];
    if (!at)
        return {};
    std::string result;
    for (;;) {
        unsigned word = phrasebook[at++];
        if (word >= phrasebook_short)
            word = ((word - phrasebook_short) << 8) + phrasebook[at++];
        if (!result.empty())
            result += ' ';
        const auto *text = lexicon + lexicon_offset[word];
        while (*text < 128)
            result += static_cast<char>(*text++);
        if (*text == 128)
            return result;
        result += static_cast<char>(*text & 127);
    }
}
std::optional<char32_t> unicode_name(std::u32string_view text)
{
    if (text.empty() || text.size() > NAME_MAXLEN)
        return {};
    std::string name;
    for (auto point : text) {
        if (point == 0 || point >= 128)
            return {};
        name += static_cast<char>(point);
    }
    if (name.starts_with("HANGUL SYLLABLE "))
        return hangul(std::string_view(name).substr(16));
    if (name.starts_with("CJK UNIFIED IDEOGRAPH-")) {
        const auto digits = std::string_view(name).substr(22);
        if (digits.size() != 4 && digits.size() != 5)
            return {};
        char32_t code = 0;
        for (auto digit : digits) {
            if (digit >= '0' && digit <= '9')
                code = code * 16 + digit - '0';
            else if (digit >= 'A' && digit <= 'F')
                code = code * 16 + digit - 'A' + 10;
            else
                return {};
        }
        return ideograph(code) ? std::optional<char32_t>{code} : std::optional<char32_t>{};
    }
    unsigned hash = 0;
    for (auto ch : name) {
        hash = hash * code_magic + static_cast<unsigned char>(upper(ch));
        if (const auto high = hash & 0xff000000U)
            hash = (hash ^ (high >> 24)) & 0x00ffffffU;
    }
    constexpr unsigned mask = code_size - 1;
    unsigned position = ~hash & mask;
    unsigned increment = (hash ^ (hash >> 3)) & mask;
    if (!increment)
        increment = mask;
    for (;;) {
        const auto code = code_hash[position];
        if (!code)
            return {};
        if (matches(code, name)) {
            if (code >= named_sequences_start && code < named_sequences_end)
                return {};
            return code >= aliases_start && code < aliases_end ? name_aliases[code - aliases_start]
                                                               : code;
        }
        position = (position + increment) & mask;
        increment <<= 1;
        if (increment > mask)
            increment ^= code_poly;
    }
}
} // namespace cxxpython::syntax
