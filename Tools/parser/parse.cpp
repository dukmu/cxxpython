#include "cxxpython/parser.hpp"

#include <fstream>
#include <iostream>
#include <iterator>

namespace {
using namespace cxxpython::syntax;
void dump(const Tree &tree, const NodePtr &node, std::size_t depth)
{
    if (node->rule == Rule::Group) {
        for (const auto &child : node->children)
            dump(tree, child, depth);
        return;
    }
    std::cout << std::string(depth * 2, ' ') << rule_name(node->rule) << ':' << node->alternative;
    if (node->rule == Rule::Token) {
        std::cout << ' ';
        for (auto ch : tree.tokens[node->begin].text)
            if (ch >= 32 && ch < 127)
                std::cout << static_cast<char>(ch);
    }
    std::cout << '\n';
    for (const auto &child : node->children)
        dump(tree, child, depth + 1);
}
} // namespace
int main(int argc, char **argv)
{
    using namespace cxxpython::syntax;
    InputMode mode = InputMode::File;
    bool show = false, tokens = false;
    for (int i = 1; i < argc; ++i) {
        const std::string_view option(argv[i]);
        if (option == "--eval")
            mode = InputMode::Eval;
        else if (option == "--single")
            mode = InputMode::Interactive;
        else if (option == "--func-type")
            mode = InputMode::FunctionType;
        else if (option == "--dump")
            show = true;
        else if (option == "--tokens")
            tokens = true;
        else {
            std::cerr << "unknown option\n";
            return 2;
        }
    }
    const std::string source{std::istreambuf_iterator<char>(std::cin),
                             std::istreambuf_iterator<char>()};
    try {
        if (tokens) {
            for (const auto &token : tokenize(source))
                std::cout << static_cast<int>(token.kind) << ' ' << token.span.begin << ' '
                          << token.span.end << '\n';
        } else {
            const auto tree = parse(source, mode);
            if (show)
                dump(tree, tree.root, 0);
            else
                std::cout << "accepted\n";
        }
    } catch (const Error &error) {
        std::cerr << error.span.begin << ':' << error.span.end << ": " << error.what() << '\n';
        return 1;
    } catch (const std::exception &error) {
        std::cerr << "parser failure: " << error.what() << '\n';
        return 2;
    }
}
