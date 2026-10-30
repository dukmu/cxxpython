#pragma once

#include "cxxpython/parser.hpp"

#include <algorithm>
#include <unordered_map>
#include <utility>

namespace cxxpython::syntax {
// Native per-parse state. The generated rules never access the Python heap.
class ParserBase {
  public:
    explicit ParserBase(std::vector<Token> tokens) : tokens_(std::move(tokens)) {}
    virtual ~ParserBase() = default;
    Tree finish(NodePtr root)
    {
        if (root && mark_ + 1 == tokens_.size() && tokens_[mark_].kind == TokenKind::ENDMARKER)
            ++mark_;
        if (!root || mark_ != tokens_.size())
            error();
        return {std::move(tokens_), std::move(root)};
    }

  protected:
    virtual bool reserved(std::u32string_view) const = 0;
    [[noreturn]] void error() const
    {
        const auto at = std::min(farthest_, tokens_.size() - 1);
        if (expected_indent_ || tokens_[at].kind == TokenKind::INDENT)
            throw Error(tokens_[at].span,
                        expected_indent_ ? "expected an indented block" : "unexpected indent",
                        ErrorKind::Indentation);
        throw Error(tokens_[at].span, "invalid syntax");
    }
    NodePtr make(Rule rule, std::size_t alternative, std::size_t begin,
                 std::vector<NodePtr> children = {}) const
    {
        // These upstream AST actions restrict accepted literals, not just their
        // representation. Keep the same checks without constructing Python ASTs.
        if (rule == Rule::real_number || rule == Rule::imaginary_number) {
            const auto &text = tokens_[begin].text;
            const bool imaginary = !text.empty() && (text.back() == U'j' || text.back() == U'J');
            if (imaginary != (rule == Rule::imaginary_number))
                throw Error(tokens_[begin].span, "invalid real or imaginary pattern literal");
        }
        if (rule == Rule::strings) {
            std::optional<bool> bytes;
            auto check = [&](auto &&self, const NodePtr &node) -> void {
                if (node->rule == Rule::Group) {
                    for (const auto &child : node->children)
                        self(self, child);
                    return;
                }
                bool current = false;
                if (node->rule == Rule::string) {
                    const auto &text = tokens_[node->begin].text;
                    const auto quote = text.find_first_of(U"'\"");
                    current = text.substr(0, quote).find_first_of(U"bB") != text.npos;
                } else if (node->rule != Rule::fstring)
                    return;
                if (bytes && *bytes != current)
                    throw Error(tokens_[node->begin].span,
                                "cannot mix bytes and nonbytes literals");
                bytes = current;
            };
            for (const auto &child : children)
                check(check, child);
        }
        return std::make_shared<const Node>(
            Node{rule, alternative, begin, mark_, std::move(children)});
    }
    NodePtr expect(TokenKind kind)
    {
        if (mark_ < tokens_.size() && tokens_[mark_].kind == kind &&
            (kind != TokenKind::NAME || !reserved(tokens_[mark_].text))) {
            const auto begin = mark_++;
            return make(Rule::Token, 0, begin);
        }
        if (mark_ > farthest_)
            expected_indent_ = false;
        if (mark_ >= farthest_ && kind == TokenKind::INDENT)
            expected_indent_ = true;
        farthest_ = std::max(farthest_, mark_);
        return {};
    }
    NodePtr expect(std::u32string_view literal)
    {
        if (mark_ < tokens_.size() && tokens_[mark_].text == literal) {
            const auto begin = mark_++;
            return make(Rule::Token, 0, begin);
        }
        if (mark_ > farthest_)
            expected_indent_ = false;
        farthest_ = std::max(farthest_, mark_);
        return {};
    }
    static bool append(std::vector<NodePtr> &children, NodePtr child)
    {
        if (!child)
            return false;
        children.push_back(std::move(child));
        return true;
    }
    NodePtr commit(bool &cut) const
    {
        cut = true;
        return make(Rule::Group, 0, mark_);
    }
    template <class F> NodePtr optional(F &&body)
    {
        const auto begin = mark_;
        auto child = body();
        if (!child) {
            mark_ = begin;
            return make(Rule::Group, 0, begin);
        }
        return make(Rule::Group, 0, begin, {std::move(child)});
    }
    template <class F> NodePtr repeat(F &&body, std::size_t minimum)
    {
        const auto begin = mark_;
        std::vector<NodePtr> children;
        for (;;) {
            const auto at = mark_;
            auto child = body();
            if (!child) {
                mark_ = at;
                break;
            }
            if (mark_ == at)
                throw std::logic_error("non-consuming grammar repetition");
            children.push_back(std::move(child));
        }
        if (children.size() < minimum) {
            mark_ = begin;
            return {};
        }
        return make(Rule::Group, 0, begin, std::move(children));
    }
    template <class S, class F> NodePtr gather(S &&separator, F &&body)
    {
        const auto begin = mark_;
        auto first = body();
        if (!first) {
            mark_ = begin;
            return {};
        }
        std::vector<NodePtr> children{std::move(first)};
        for (;;) {
            const auto at = mark_;
            auto sep = separator();
            auto child = sep ? body() : NodePtr{};
            if (!child) {
                mark_ = at;
                break;
            }
            if (mark_ == at)
                throw std::logic_error("non-consuming grammar gather");
            children.push_back(std::move(sep));
            children.push_back(std::move(child));
        }
        return make(Rule::Group, 0, begin, std::move(children));
    }
    template <class F> NodePtr lookahead(bool positive, F &&body)
    {
        const auto begin = mark_, diagnostic = farthest_;
        const auto indent = expected_indent_;
        const bool matched = static_cast<bool>(body());
        mark_ = begin;
        farthest_ = diagnostic;
        expected_indent_ = indent;
        return matched == positive ? make(Rule::Group, 0, begin) : NodePtr{};
    }
    template <class F> NodePtr required(F &&body)
    {
        auto result = body();
        if (!result)
            error();
        return result;
    }
    template <class F> NodePtr memo(Rule rule, bool recursive, bool leader, F &&body)
    {
        const Key key{mark_, rule};
        if (!recursive || leader) {
            if (const auto found = memo_.find(key); found != memo_.end()) {
                mark_ = found->second.end;
                return found->second.node;
            }
        }
        if (++depth_ > 4096) {
            --depth_;
            throw Error(tokens_[std::min(mark_, tokens_.size() - 1)].span,
                        "parser nesting limit exceeded");
        }
        struct Exit {
            std::size_t &depth;
            ~Exit()
            {
                --depth;
            }
        } exit{depth_};
        if (recursive && !leader)
            return body();
        if (!recursive) {
            auto node = body();
            memo_.emplace(key, Memo{node, mark_});
            return node;
        }
        // pegen's SCC leader grows a seed; non-leaders in the SCC are not memoized.
        memo_.emplace(key, Memo{{}, key.mark});
        NodePtr best;
        auto end = key.mark;
        for (;;) {
            mark_ = key.mark;
            auto node = body();
            if (!node || mark_ <= end)
                break;
            best = std::move(node);
            end = mark_;
            memo_[key] = {best, end};
        }
        mark_ = end;
        return best;
    }
    std::size_t mark_ = 0;

  private:
    struct Key {
        std::size_t mark;
        Rule rule;
        bool operator==(const Key &) const = default;
    };
    struct KeyHash {
        std::size_t operator()(Key key) const noexcept
        {
            return key.mark * (grammar_rule_count + 2) + static_cast<std::size_t>(key.rule);
        }
    };
    struct Memo {
        NodePtr node;
        std::size_t end;
    };
    std::vector<Token> tokens_;
    std::unordered_map<Key, Memo, KeyHash> memo_;
    std::size_t farthest_ = 0, depth_ = 0;
    bool expected_indent_ = false;
};
} // namespace cxxpython::syntax
