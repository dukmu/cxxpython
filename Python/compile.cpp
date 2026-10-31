#include "cxxpython/complexobject.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "internal/compiler.hpp"

#include <boost/multiprecision/cpp_int.hpp>

#include <algorithm>
#include <cmath>
#include <complex>

namespace cxxpython::core::detail {
namespace {
using PatternKey = std::variant<Literal, boost::multiprecision::cpp_int, std::complex<double>>;
std::optional<PatternKey> pattern_key(const Expr &expression)
{
    using Integer = boost::multiprecision::cpp_int;
    using Complex = std::complex<double>;
    if (const auto *value = std::get_if<Literal>(&expression.node)) {
        if (value->kind == Literal::Kind::True || value->kind == Literal::Kind::False)
            return Integer(value->kind == Literal::Kind::True);
        if (value->kind == Literal::Kind::Float)
            return Complex(value->number, 0);
        if (value->kind == Literal::Kind::Imaginary)
            return Complex(0, value->number);
        if (value->kind != Literal::Kind::Integer)
            return *value;
        Integer result = 0;
        unsigned base = 10;
        std::size_t start = 0;
        const auto &text = value->text;
        if (text.size() > 1 && text[0] == U'0') {
            auto prefix = text[1];
            base = prefix == U'x' || prefix == U'X'   ? 16
                   : prefix == U'o' || prefix == U'O' ? 8
                   : prefix == U'b' || prefix == U'B' ? 2
                                                      : 10;
            if (base != 10)
                start = 2;
        }
        for (std::size_t i = start; i < text.size(); ++i)
            if (text[i] != U'_') {
                const auto ch = text[i];
                result *= base;
                result += ch <= U'9' ? ch - U'0' : ch <= U'F' ? ch - U'A' + 10 : ch - U'a' + 10;
            }
        return result;
    }
    if (const auto *unary = std::get_if<UnaryExpr>(&expression.node)) {
        auto value = pattern_key(*unary->value);
        if (!value || unary->op != UnaryOp::Negative)
            return {};
        if (auto integer = std::get_if<Integer>(&*value))
            return Integer(-*integer);
        if (auto number = std::get_if<Complex>(&*value))
            return -*number;
    }
    if (const auto *binary = std::get_if<BinaryExpr>(&expression.node)) {
        auto left = pattern_key(*binary->left), right = pattern_key(*binary->right);
        if (!left || !right || !std::holds_alternative<Complex>(*right))
            return {};
        Complex number;
        if (auto integer = std::get_if<Integer>(&*left)) {
            const auto real = integer->convert_to<double>();
            if (!std::isfinite(real))
                return {};
            number = Complex(real, 0);
        } else if (auto real = std::get_if<Complex>(&*left))
            number = *real;
        else
            return {};
        return binary->op == BinaryOp::Add ? number + std::get<Complex>(*right)
                                           : number - std::get<Complex>(*right);
    }
    return {};
}
bool equal_pattern_keys(const PatternKey &left, const PatternKey &right)
{
    using Integer = boost::multiprecision::cpp_int;
    using Complex = std::complex<double>;
    if (auto value = std::get_if<Literal>(&left)) {
        auto other = std::get_if<Literal>(&right);
        return other && value->kind == other->kind && value->text == other->text;
    }
    if (auto value = std::get_if<Integer>(&left)) {
        if (auto other = std::get_if<Integer>(&right))
            return *value == *other;
        if (auto other = std::get_if<Complex>(&right))
            return other->imag() == 0 && std::isfinite(other->real()) &&
                   std::trunc(other->real()) == other->real() && Integer(other->real()) == *value;
        return false;
    }
    if (std::holds_alternative<Integer>(right))
        return equal_pattern_keys(right, left);
    auto other = std::get_if<Complex>(&right);
    return other && std::get<Complex>(left) == *other;
}
class GraphPass final {
  public:
    explicit GraphPass(SourceProgram &program) : program_(program)
    {
        program_.graphs.resize(program_.scopes.size());
    }
    void build(const Suite &body)
    {
        function(0, body);
    }

  private:
    [[noreturn]] void error(Span span, std::string message) const
    {
        throw SourceError(program_.source, span, std::move(message));
    }
    FlowBlock &current()
    {
        return program_.graphs[scope_].blocks[block_];
    }
    std::size_t block()
    {
        auto &blocks = program_.graphs[scope_].blocks;
        blocks.emplace_back();
        return blocks.size() - 1;
    }
    void select(std::size_t id)
    {
        block_ = id;
        program_.graphs[scope_].order.push_back(id);
    }
    std::size_t region(RegionKind kind, Span span, std::optional<std::size_t> continued = {},
                       std::optional<std::size_t> broken = {})
    {
        auto &regions = program_.graphs[scope_].regions;
        const auto id = regions.size();
        regions.push_back({kind, continued, broken, {}});
        emit(Opcode::EnterRegion, span, static_cast<std::uint32_t>(id));
        return id;
    }
    void emit(Opcode opcode, Span span, std::uint32_t operand = 0, std::u32string name = {})
    {
        current().instructions.push_back({opcode, span, operand, std::move(name), {}, {}, {}});
    }
    void literal(Literal value, Span span)
    {
        emit(Opcode::LoadConst, span);
        current().instructions.back().literal = std::move(value);
    }
    void none(Span span)
    {
        literal({Literal::Kind::None, {}}, span);
    }
    void jump(std::size_t target, Span span)
    {
        current().exit = FlowBlock::Exit::Jump;
        current().target = target;
        current().span = span;
    }
    void branch(std::size_t yes, std::size_t no, Span span)
    {
        current().exit = FlowBlock::Exit::Branch;
        current().target = yes;
        current().alternative = no;
        current().span = span;
    }
    const SymbolInfo &symbol(const std::u32string &name) const
    {
        const auto &symbols = program_.scopes[scope_].symbols;
        auto found = std::find_if(symbols.begin(), symbols.end(),
                                  [&](const auto &entry) { return entry.name == name; });
        if (found == symbols.end())
            throw std::logic_error("scope pass omitted a name");
        return *found;
    }
    void name(const std::u32string &text, Span span, Opcode local, Opcode global)
    {
        const auto &binding = symbol(text);
        if (binding.classification == NameClass::Namespace ||
            (program_.scopes[scope_].kind != ScopeKind::Function &&
             binding.classification == NameClass::GlobalImplicit))
            emit(local == Opcode::LoadFast    ? Opcode::LoadName
                 : local == Opcode::StoreFast ? Opcode::StoreName
                                              : Opcode::DeleteName,
                 span, 0, text);
        else if (binding.classification == NameClass::Local)
            emit(local, span, static_cast<std::uint32_t>(*binding.slot));
        else if (binding.classification == NameClass::Cell ||
                 binding.classification == NameClass::Free)
            emit(local == Opcode::LoadFast
                     ? (program_.scopes[scope_].kind == ScopeKind::Class ? Opcode::LoadClassDeref
                                                                         : Opcode::LoadDeref)
                 : local == Opcode::StoreFast ? Opcode::StoreDeref
                                              : Opcode::DeleteDeref,
                 span, static_cast<std::uint32_t>(*binding.slot));
        else
            emit(global, span, 0, text);
    }
    void nested(std::size_t scope, const Suite &body, const FunctionStmt *definition = nullptr)
    {
        const auto parent_scope = scope_, parent_block = block_;
        auto parent_loops = std::move(loops_);
        auto parent_star_handlers = std::move(star_handlers_);
        const auto *parent_first = first_statement_;
        function(scope, body, definition);
        scope_ = parent_scope;
        block_ = parent_block;
        loops_ = std::move(parent_loops);
        star_handlers_ = std::move(parent_star_handlers);
        first_statement_ = parent_first;
    }
    void make_function(std::size_t child, Span span, std::uint32_t flags = 0)
    {
        const auto &parent = program_.scopes[scope_];
        const auto &freevars = program_.scopes[child].freevars;
        for (const auto &name : freevars) {
            auto cell = std::find(parent.cellvars.begin(), parent.cellvars.end(), name);
            std::size_t slot = cell - parent.cellvars.begin();
            if (cell == parent.cellvars.end()) {
                const auto free = std::find(parent.freevars.begin(), parent.freevars.end(), name);
                if (free == parent.freevars.end())
                    throw std::logic_error("child free variable has no enclosing cell");
                slot += free - parent.freevars.begin();
            }
            emit(Opcode::LoadClosure, span, static_cast<std::uint32_t>(slot));
        }
        if (!freevars.empty())
            emit(Opcode::BuildTuple, span, static_cast<std::uint32_t>(freevars.size()));
        emit(Opcode::LoadConst, span);
        current().instructions.back().function = child;
        emit(Opcode::MakeFunction, span, flags | (!freevars.empty() ? 4 : 0));
    }
    void function_value(const FunctionStmt &node, Span span)
    {
        nested(node.scope, node.body, &node);
        std::uint32_t defaults = 0;
        std::vector<std::u32string> keywords;
        for (const auto &parameter : node.parameters)
            if (parameter.default_value && parameter.kind != ParameterKind::KeywordOnly) {
                expression(*parameter.default_value);
                ++defaults;
            }
        if (defaults)
            emit(Opcode::BuildTuple, span, defaults);
        for (const auto &parameter : node.parameters)
            if (parameter.default_value && parameter.kind == ParameterKind::KeywordOnly) {
                expression(*parameter.default_value);
                keywords.push_back(parameter.name);
            }
        if (!keywords.empty()) {
            emit(Opcode::BuildNameDict, span);
            current().instructions.back().keywords = keywords;
        }
        std::vector<std::u32string> annotations;
        for (const auto &annotation : node.annotations) {
            expression(*annotation.second);
            annotations.push_back(annotation.first);
        }
        if (!annotations.empty()) {
            emit(Opcode::BuildNameDict, span);
            current().instructions.back().keywords = std::move(annotations);
        }
        make_function(node.scope, span,
                      (defaults ? 1 : 0) | (!keywords.empty() ? 2 : 0) |
                          (!node.annotations.empty() ? 8 : 0));
    }
    void sequence(const std::vector<ExprPtr> &items, Span span, bool tuple)
    {
        const bool unpack = std::any_of(items.begin(), items.end(), [](const auto &item) {
            return std::holds_alternative<StarredExpr>(item->node);
        });
        if (!unpack) {
            for (const auto &item : items)
                expression(*item);
            emit(tuple ? Opcode::BuildTuple : Opcode::BuildList, span,
                 static_cast<std::uint32_t>(items.size()));
            return;
        }
        emit(Opcode::BuildList, span);
        for (const auto &item : items) {
            if (const auto *star = std::get_if<StarredExpr>(&item->node)) {
                expression(*star->value);
                emit(Opcode::ListExtend, item->span);
            } else {
                expression(*item);
                emit(Opcode::ListAppend, item->span);
            }
        }
        if (tuple)
            emit(Opcode::ListToTuple, span);
    }
    void test(const Expr &value, std::size_t yes, std::size_t no, bool preserve = false)
    {
        // Jump-context lowering must not repeat an observable __bool__.
        if (const auto *node = std::get_if<BoolExpr>(&value.node)) {
            for (std::size_t i = 0; i + 1 < node->values.size(); ++i) {
                const auto next = block();
                test(*node->values[i], node->conjunction ? next : yes,
                     node->conjunction ? no : next, preserve);
                select(next);
                if (preserve)
                    emit(Opcode::PopTop, value.span);
            }
            test(*node->values.back(), yes, no, preserve);
            return;
        }
        if (const auto *node = std::get_if<CompareExpr>(&value.node)) {
            expression(*node->values[0]);
            for (std::size_t i = 0; i < node->ops.size(); ++i) {
                expression(*node->values[i + 1]);
                if (i + 1 < node->ops.size()) {
                    emit(Opcode::DupTop, value.span);
                    emit(Opcode::Swap, value.span, 3);
                    emit(Opcode::Swap, value.span, 2);
                }
                emit(Opcode::Compare, value.span, static_cast<std::uint32_t>(node->ops[i]));
                if (preserve)
                    emit(Opcode::DupTop, value.span);
                if (i + 1 == node->ops.size()) {
                    branch(yes, no, value.span);
                    return;
                }
                const auto again = block(), failed = block();
                branch(again, failed, value.span);
                select(failed);
                if (preserve)
                    emit(Opcode::Swap, value.span, 2);
                emit(Opcode::PopTop, value.span);
                jump(no, value.span);
                select(again);
                if (preserve)
                    emit(Opcode::PopTop, value.span);
            }
        } else {
            expression(value);
            if (preserve)
                emit(Opcode::DupTop, value.span);
            branch(yes, no, value.span);
        }
    }
    void expression(const Expr &value)
    {
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, Literal>)
                    literal(node, value.span);
                else if constexpr (std::is_same_v<T, NameExpr>) {
                    if (node.name == U"__debug__")
                        literal({Literal::Kind::True, {}}, value.span);
                    else
                        name(node.name, value.span, Opcode::LoadFast, Opcode::LoadGlobal);
                } else if constexpr (std::is_same_v<T, UnaryExpr>) {
                    expression(*node.value);
                    emit(Opcode::Unary, value.span, static_cast<std::uint32_t>(node.op));
                } else if constexpr (std::is_same_v<T, BinaryExpr>) {
                    expression(*node.left);
                    expression(*node.right);
                    emit(Opcode::Binary, value.span, static_cast<std::uint32_t>(node.op));
                } else if constexpr (std::is_same_v<T, AttributeExpr>) {
                    expression(*node.object);
                    emit(Opcode::LoadAttr, value.span, 0, node.name);
                } else if constexpr (std::is_same_v<T, TupleExpr> || std::is_same_v<T, ListExpr>) {
                    sequence(node.items, value.span, std::is_same_v<T, TupleExpr>);
                } else if constexpr (std::is_same_v<T, SetExpr>) {
                    std::uint32_t pending = 0;
                    bool built = false;
                    for (const auto &item : node.items) {
                        if (const auto *star = std::get_if<StarredExpr>(&item->node)) {
                            if (!built) {
                                emit(Opcode::BuildSet, value.span, pending);
                                built = true;
                            }
                            expression(*star->value);
                            emit(Opcode::SetUpdate, item->span);
                        } else {
                            expression(*item);
                            if (built)
                                emit(Opcode::SetAdd, item->span);
                            else
                                ++pending;
                        }
                    }
                    if (!built)
                        emit(Opcode::BuildSet, value.span, pending);
                } else if constexpr (std::is_same_v<T, DictExpr>) {
                    std::uint32_t pending = 0;
                    bool built = false;
                    auto flush = [&] {
                        if (!pending && built)
                            return;
                        emit(Opcode::BuildMap, value.span, pending);
                        if (built)
                            emit(Opcode::DictUpdate, value.span);
                        built = true;
                        pending = 0;
                    };
                    for (const auto &[key, item] : node.items) {
                        if (key) {
                            expression(*key);
                            expression(*item);
                            ++pending;
                        } else {
                            flush();
                            expression(*item);
                            emit(Opcode::DictUpdate, item->span);
                        }
                    }
                    flush();
                } else if constexpr (std::is_same_v<T, NamedExpr>) {
                    expression(*node.value);
                    emit(Opcode::DupTop, value.span);
                    store(*node.target);
                } else if constexpr (std::is_same_v<T, ConditionalExpr>) {
                    const auto yes = block(), no = block(), done = block();
                    test(*node.test, yes, no);
                    select(yes);
                    expression(*node.body);
                    jump(done, value.span);
                    select(no);
                    expression(*node.alternative);
                    jump(done, value.span);
                    select(done);
                } else if constexpr (std::is_same_v<T, LambdaExpr>) {
                    function_value(*node.function, value.span);
                } else if constexpr (std::is_same_v<T, YieldExpr>) {
                    if (node.value)
                        expression(*node.value);
                    else
                        none(value.span);
                    if (node.delegated)
                        emit(Opcode::GetIter, value.span);
                    emit(node.delegated                      ? Opcode::YieldFrom
                         : program_.scopes[scope_].coroutine ? Opcode::AsyncYieldValue
                                                             : Opcode::YieldValue,
                         value.span);
                } else if constexpr (std::is_same_v<T, AwaitExpr>) {
                    expression(*node.value);
                    emit(Opcode::GetAwaitable, value.span);
                    emit(Opcode::YieldFrom, value.span);
                } else if constexpr (std::is_same_v<T, ComprehensionExpr>) {
                    function_value(*node.function, value.span);
                    expression(*node.iterable);
                    emit(node.asynchronous_iterable ? Opcode::GetAiter : Opcode::GetIter,
                         value.span);
                    emit(Opcode::Call, value.span, 1);
                    const auto &child = program_.scopes[node.function->scope];
                    if (child.coroutine && !child.generator) {
                        emit(Opcode::GetAwaitable, value.span);
                        emit(Opcode::YieldFrom, value.span);
                    }
                } else if constexpr (std::is_same_v<T, StarredExpr>) {
                    error(value.span, "starred expression cannot be used here");
                } else if constexpr (std::is_same_v<T, SubscriptExpr>) {
                    expression(*node.object);
                    expression(*node.key);
                    emit(Opcode::LoadItem, value.span);
                } else if constexpr (std::is_same_v<T, SliceExpr>) {
                    for (const auto *item : {&node.start, &node.stop, &node.step}) {
                        if (*item)
                            expression(**item);
                        else
                            none(value.span);
                    }
                    emit(Opcode::BuildSlice, value.span);
                } else if constexpr (std::is_same_v<T, JoinedStrExpr>) {
                    for (const auto &item : node.items)
                        expression(*item);
                    emit(Opcode::BuildString, value.span,
                         static_cast<std::uint32_t>(node.items.size()));
                } else if constexpr (std::is_same_v<T, FormattedExpr>) {
                    expression(*node.value);
                    expression(*node.spec);
                    emit(Opcode::FormatValue, value.span,
                         node.conversion == 's'   ? 1
                         : node.conversion == 'r' ? 2
                         : node.conversion == 'a' ? 3
                                                  : 0);
                } else if constexpr (std::is_same_v<T, BoolExpr>) {
                    const auto done = block();
                    for (std::size_t i = 0; i + 1 < node.values.size(); ++i) {
                        const auto next = block();
                        test(*node.values[i], node.conjunction ? next : done,
                             node.conjunction ? done : next, true);
                        select(next);
                        emit(Opcode::PopTop, value.span);
                    }
                    expression(*node.values.back());
                    jump(done, value.span);
                    select(done);
                } else if constexpr (std::is_same_v<T, CompareExpr>) {
                    expression(*node.values[0]);
                    const auto done = block();
                    for (std::size_t i = 0; i < node.ops.size(); ++i) {
                        expression(*node.values[i + 1]);
                        if (i + 1 < node.ops.size()) {
                            emit(Opcode::DupTop, value.span);
                            emit(Opcode::Swap, value.span, 3);
                            emit(Opcode::Swap, value.span, 2);
                        }
                        emit(Opcode::Compare, value.span, static_cast<std::uint32_t>(node.ops[i]));
                        if (i + 1 < node.ops.size()) {
                            emit(Opcode::DupTop, value.span);
                            const auto again = block(), failed = block();
                            branch(again, failed, value.span);
                            select(failed);
                            emit(Opcode::Swap, value.span, 2);
                            emit(Opcode::PopTop, value.span);
                            jump(done, value.span);
                            select(again);
                            emit(Opcode::PopTop, value.span);
                        }
                    }
                    jump(done, value.span);
                    select(done);
                } else if constexpr (std::is_same_v<T, CallExpr>) {
                    expression(*node.target);
                    const bool unpack =
                        std::any_of(node.positional.begin(), node.positional.end(),
                                    [](const auto &item) {
                                        return std::holds_alternative<StarredExpr>(item->node);
                                    }) ||
                        std::any_of(node.keywords.begin(), node.keywords.end(),
                                    [](const auto &item) { return item.first.empty(); });
                    if (unpack) {
                        sequence(node.positional, value.span, true);
                        emit(Opcode::BuildMap, value.span);
                        for (const auto &[name, item] : node.keywords) {
                            if (!name.empty())
                                literal({Literal::Kind::String, name}, item->span);
                            expression(*item);
                            if (!name.empty())
                                emit(Opcode::BuildMap, item->span, 1);
                            emit(Opcode::DictMerge, item->span);
                        }
                        emit(Opcode::CallEx, value.span);
                        return;
                    }
                    for (const auto &item : node.positional)
                        expression(*item);
                    for (const auto &item : node.keywords)
                        expression(*item.second);
                    emit(Opcode::Call, value.span,
                         static_cast<std::uint32_t>(node.positional.size()));
                    for (const auto &item : node.keywords)
                        current().instructions.back().keywords.push_back(item.first);
                } else
                    static_assert(sizeof(T) == 0, "CFG lowering missing expression");
            },
            value.node);
    }
    void store(const Expr &target, bool erase = false)
    {
        auto unpack = [&](const auto &items) {
            auto star = std::find_if(items.begin(), items.end(), [](const auto &item) {
                return std::holds_alternative<StarredExpr>(item->node);
            });
            if (star == items.end())
                emit(Opcode::UnpackSequence, target.span, static_cast<std::uint32_t>(items.size()));
            else {
                const auto before = static_cast<std::size_t>(star - items.begin());
                const auto after = items.size() - before - 1;
                if (before > 65535 || after > 65535)
                    error(target.span, "too many expressions in star-unpacking assignment");
                emit(Opcode::UnpackEx, target.span,
                     static_cast<std::uint32_t>(before | (after << 16)));
            }
        };
        if (const auto *tuple = std::get_if<TupleExpr>(&target.node)) {
            if (!erase)
                unpack(tuple->items);
            for (const auto &item : tuple->items)
                store(*item, erase);
        } else if (const auto *list = std::get_if<ListExpr>(&target.node)) {
            if (!erase)
                unpack(list->items);
            for (const auto &item : list->items)
                store(*item, erase);
        } else if (const auto *star = std::get_if<StarredExpr>(&target.node))
            store(*star->value, erase);
        else if (const auto *entry = std::get_if<NameExpr>(&target.node))
            name(entry->name, target.span, erase ? Opcode::DeleteFast : Opcode::StoreFast,
                 erase ? Opcode::DeleteGlobal : Opcode::StoreGlobal);
        else if (const auto *item = std::get_if<SubscriptExpr>(&target.node)) {
            expression(*item->object);
            expression(*item->key);
            if (!erase) {
                emit(Opcode::Swap, target.span, 2);
                emit(Opcode::Swap, target.span, 3);
            }
            emit(erase ? Opcode::DeleteItem : Opcode::StoreItem, target.span);
        } else {
            const auto &attribute = std::get<AttributeExpr>(target.node);
            expression(*attribute.object);
            if (!erase)
                emit(Opcode::Swap, target.span, 2);
            emit(erase ? Opcode::DeleteAttr : Opcode::StoreAttr, target.span, 0, attribute.name);
        }
    }
    void close_protected(std::size_t id, Span span)
    {
        const auto leave = block();
        if (current().exit == FlowBlock::Exit::Open)
            jump(leave, span);
        select(leave);
        current().exit = FlowBlock::Exit::Leave;
        current().target = id;
        current().span = span;
    }
    void close_handler(std::size_t id, Span span)
    {
        const auto end = block();
        if (current().exit == FlowBlock::Exit::Open)
            jump(end, span);
        select(end);
        current().exit = FlowBlock::Exit::EndHandler;
        current().target = id;
        current().span = span;
    }
    void finally_handler(std::size_t id, const Suite &cleanup, Span span)
    {
        close_protected(id, span);
        select(block());
        current().handler = id;
        suite(cleanup);
        close_handler(id, span);
        select(block());
    }
    void try_except(const TryStmt &node, Span span)
    {
        if (node.starred) {
            try_star_except(node, span);
            return;
        }
        if (node.handlers.empty()) {
            suite(node.body);
            return;
        }
        const auto id = region(RegionKind::Except, span);
        const auto normal = block(), done = block(), finished = block();
        program_.graphs[scope_].regions[id].normal_target = normal;
        suite(node.body);
        close_protected(id, span);
        select(block());
        current().handler = id;
        for (const auto &handler : node.handlers) {
            std::optional<std::size_t> next;
            if (handler.type) {
                emit(Opcode::DupTop, handler.span);
                expression(*handler.type);
                emit(Opcode::ExceptionMatch, handler.span);
                const auto matched = block();
                next = block();
                branch(matched, *next, handler.span);
                select(matched);
            }
            if (handler.name) {
                const auto &[text, location] = *handler.name;
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
                const auto cleanup = region(RegionKind::Finally, handler.span);
                suite(handler.body);
                close_protected(cleanup, handler.span);
                select(block());
                current().handler = cleanup;
                none(location);
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
                name(text, location, Opcode::DeleteFast, Opcode::DeleteGlobal);
                close_handler(cleanup, handler.span);
                select(block());
            } else {
                emit(Opcode::PopTop, handler.span);
                suite(handler.body);
            }
            if (current().exit == FlowBlock::Exit::Open)
                jump(finished, handler.span);
            if (next)
                select(*next);
            else
                break;
        }
        if (node.handlers.back().type) {
            emit(Opcode::Reraise, span);
            current().exit = FlowBlock::Exit::Stop;
        }
        select(finished);
        current().exit = FlowBlock::Exit::EndHandler;
        current().target = id;
        current().span = span;
        select(block());
        jump(done, span);
        select(normal);
        suite(node.alternative);
        if (current().exit == FlowBlock::Exit::Open)
            jump(done, span);
        select(done);
    }
    void try_star_except(const TryStmt &node, Span span)
    {
        const auto id = region(RegionKind::Except, span);
        const auto normal = block(), done = block();
        program_.graphs[scope_].regions[id].normal_target = normal;
        suite(node.body);
        close_protected(id, span);
        select(block());
        current().handler = id;
        // The existing traced operand stack owns original, results, remainder.
        emit(Opcode::BuildList, span);
        emit(Opcode::Copy, span, 2);
        for (const auto &handler : node.handlers) {
            expression(*handler.type);
            emit(Opcode::CheckEgMatch, handler.span);
            emit(Opcode::DupTop, handler.span);
            none(handler.span);
            emit(Opcode::Compare, handler.span, static_cast<std::uint32_t>(CompareOp::Is));
            const auto unmatched = block(), matched = block(), next = block();
            branch(unmatched, matched, handler.span);
            select(unmatched);
            emit(Opcode::PopTop, handler.span);
            jump(next, handler.span);
            select(matched);
            if (handler.name) {
                const auto &[text, location] = *handler.name;
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
            } else
                emit(Opcode::PopTop, handler.span);
            const auto capture = region(RegionKind::Except, handler.span);
            std::optional<std::size_t> cleanup;
            if (handler.name)
                cleanup = region(RegionKind::Finally, handler.span);
            star_handlers_.push_back(loops_.size());
            suite(handler.body);
            star_handlers_.pop_back();
            if (cleanup) {
                close_protected(*cleanup, handler.span);
                select(block());
                current().handler = *cleanup;
                const auto &[text, location] = *handler.name;
                none(location);
                name(text, location, Opcode::StoreFast, Opcode::StoreGlobal);
                name(text, location, Opcode::DeleteFast, Opcode::DeleteGlobal);
                close_handler(*cleanup, handler.span);
                select(block());
            }
            close_protected(capture, handler.span);
            select(block());
            current().handler = capture;
            // Capture through ordinary unwind; the remainder stays below the error.
            emit(Opcode::Copy, handler.span, 3);
            emit(Opcode::Swap, handler.span, 2);
            emit(Opcode::ListAppend, handler.span);
            emit(Opcode::PopTop, handler.span);
            close_handler(capture, handler.span);
            select(block());
            jump(next, handler.span);
            select(next);
        }
        emit(Opcode::ListAppend, span);
        emit(Opcode::PrepReraiseStar, span);
        emit(Opcode::DupTop, span);
        none(span);
        emit(Opcode::Compare, span, static_cast<std::uint32_t>(CompareOp::Is));
        const auto handled = block(), reraised = block();
        branch(handled, reraised, span);
        select(reraised);
        emit(Opcode::Reraise, span, 1);
        current().exit = FlowBlock::Exit::Stop;
        select(handled);
        emit(Opcode::PopTop, span);
        close_handler(id, span);
        select(block());
        jump(done, span);
        select(normal);
        suite(node.alternative);
        if (current().exit == FlowBlock::Exit::Open)
            jump(done, span);
        select(done);
    }
    struct PatternFailure {
        std::size_t block, pops, target;
        Span span;
    };
    struct PatternContext {
        std::vector<const PatternCapture *> captures;
        std::vector<PatternFailure> failures;
    };
    std::size_t pattern_failure(PatternContext &context, std::size_t pops, std::size_t target,
                                Span span)
    {
        if (!pops)
            return target;
        const auto failed = block();
        context.failures.push_back({failed, pops, target, span});
        return failed;
    }
    void pattern_test(std::size_t failed, Span span)
    {
        const auto passed = block();
        branch(passed, failed, span);
        select(passed);
    }
    void pattern_not_none(PatternContext &context, std::size_t failed, Span span)
    {
        emit(Opcode::DupTop, span);
        none(span);
        emit(Opcode::Compare, span, static_cast<std::uint32_t>(CompareOp::IsNot));
        pattern_test(pattern_failure(context, 1, failed, span), span);
    }
    void pattern_remove_container(std::size_t captures, Span span)
    {
        for (std::size_t depth = 2; depth <= captures + 1; ++depth)
            emit(Opcode::Swap, span, static_cast<std::uint32_t>(depth));
        emit(Opcode::PopTop, span);
    }
    void pattern_permute(std::vector<const PatternCapture *> actual,
                         const std::vector<const PatternCapture *> &wanted, Span span)
    {
        const auto count = actual.size();
        for (std::size_t i = 0; i < count; ++i) {
            auto found = std::find_if(actual.begin() + i, actual.end(),
                                      [&](auto item) { return item->name == wanted[i]->name; });
            if (found == actual.end())
                throw std::logic_error("scope pass accepted inconsistent pattern bindings");
            const auto j = static_cast<std::size_t>(found - actual.begin());
            if (i == j)
                continue;
            const auto a = static_cast<std::uint32_t>(count - i);
            const auto b = static_cast<std::uint32_t>(count - j);
            if (b == 1)
                emit(Opcode::Swap, span, a);
            else {
                emit(Opcode::Swap, span, a);
                emit(Opcode::Swap, span, b);
                emit(Opcode::Swap, span, a);
            }
            std::swap(actual[i], actual[j]);
        }
    }
    void pattern_tuple(const std::vector<const Pattern *> &items, PatternContext &context,
                       std::size_t failed, Span span, bool reverse = false)
    {
        const auto start = context.captures.size();
        for (std::size_t i = 0; i < items.size(); ++i) {
            const auto captured = context.captures.size() - start;
            emit(Opcode::Copy, span, static_cast<std::uint32_t>(captured + 1));
            literal({Literal::Kind::Integer, std::u32string{}}, span);
            auto number = std::to_string(reverse ? items.size() - i - 1 : i);
            current().instructions.back().literal->text.assign(number.begin(), number.end());
            emit(Opcode::LoadItem, span);
            pattern(*items[i], context, pattern_failure(context, captured + 1, failed, span));
        }
        pattern_remove_container(context.captures.size() - start, span);
    }
    void pattern_length(std::size_t required, bool exact, std::size_t failed, Span span)
    {
        emit(Opcode::GetLen, span);
        const auto text = std::to_string(required);
        literal({Literal::Kind::Integer, {text.begin(), text.end()}}, span);
        emit(Opcode::Compare, span,
             static_cast<std::uint32_t>(exact ? CompareOp::Eq : CompareOp::Ge));
        pattern_test(failed, span);
    }
    void annotation_target(const Expr &value)
    {
        if (const auto *slice = std::get_if<SliceExpr>(&value.node)) {
            for (const auto *item : {&slice->start, &slice->stop, &slice->step})
                if (*item) {
                    expression(**item);
                    emit(Opcode::PopTop, (*item)->span);
                }
        } else if (const auto *tuple = std::get_if<TupleExpr>(&value.node)) {
            for (const auto &item : tuple->items)
                annotation_target(*item);
        } else {
            expression(value);
            emit(Opcode::PopTop, value.span);
        }
    }
    void annotated_assignment(const AnnAssignStmt &node, Span span)
    {
        if (node.value) {
            expression(*node.value);
            store(*node.target);
        } else if (const auto *attribute = std::get_if<AttributeExpr>(&node.target->node)) {
            expression(*attribute->object);
            emit(Opcode::PopTop, span);
        } else if (const auto *item = std::get_if<SubscriptExpr>(&node.target->node)) {
            expression(*item->object);
            emit(Opcode::PopTop, span);
            annotation_target(*item->key);
        }
        const auto kind = program_.scopes[scope_].kind;
        if (node.simple && kind != ScopeKind::Function) {
            expression(*node.annotation);
            emit(Opcode::LoadName, span, 0, U"__annotations__");
            literal({Literal::Kind::String, std::get<NameExpr>(node.target->node).name}, span);
            emit(Opcode::Swap, span, 2);
            emit(Opcode::Swap, span, 3);
            emit(Opcode::StoreItem, span);
        } else if (!node.simple && kind != ScopeKind::Function &&
                   !(program_.future_flags & future_annotations)) {
            expression(*node.annotation);
            emit(Opcode::PopTop, span);
        }
    }
    void pattern(const Pattern &value, PatternContext &context, std::size_t failed)
    {
        const auto start = context.captures.size();
        const auto span = value.span;
        auto capture = [&](const PatternCapture &target) {
            if (target.binding)
                context.captures.push_back(&target);
            else
                emit(Opcode::PopTop, span);
        };
        std::visit(
            [&](const auto &node) {
                using T = std::decay_t<decltype(node)>;
                if constexpr (std::is_same_v<T, ValuePattern>) {
                    expression(*node.value);
                    emit(
                        Opcode::Compare, span,
                        static_cast<std::uint32_t>(node.singleton ? CompareOp::Is : CompareOp::Eq));
                    pattern_test(failed, span);
                } else if constexpr (std::is_same_v<T, CapturePattern> ||
                                     std::is_same_v<T, StarPattern>)
                    capture(node.target);
                else if constexpr (std::is_same_v<T, AsPattern>) {
                    emit(Opcode::DupTop, span);
                    pattern(*node.pattern, context, pattern_failure(context, 1, failed, span));
                    for (std::size_t depth = 2; depth <= context.captures.size() - start + 1;
                         ++depth)
                        emit(Opcode::Swap, span, static_cast<std::uint32_t>(depth));
                    capture(node.target);
                } else if constexpr (std::is_same_v<T, OrPattern>) {
                    const auto saved = context.captures;
                    const auto matched = block();
                    std::vector<const PatternCapture *> expected;
                    for (std::size_t i = 0; i < node.items.size(); ++i) {
                        const auto next = block();
                        context.captures = saved;
                        emit(Opcode::DupTop, span);
                        pattern(*node.items[i], context, next);
                        std::vector<const PatternCapture *> actual(context.captures.begin() + start,
                                                                   context.captures.end());
                        if (!i)
                            expected = actual;
                        else
                            pattern_permute(actual, expected, span);
                        pattern_remove_container(expected.size(), span);
                        jump(matched, span);
                        select(next);
                    }
                    emit(Opcode::PopTop, span);
                    jump(failed, span);
                    select(matched);
                    context.captures = saved;
                    context.captures.insert(context.captures.end(), expected.begin(),
                                            expected.end());
                } else if constexpr (std::is_same_v<T, SequencePattern>) {
                    const auto drop = pattern_failure(context, 1, failed, span);
                    emit(Opcode::MatchSequence, span);
                    pattern_test(drop, span);
                    std::optional<std::size_t> star;
                    bool only_wildcards = true, star_wildcard = false;
                    std::vector<const Pattern *> items;
                    for (std::size_t i = 0; i < node.items.size(); ++i) {
                        const auto &item = *node.items[i];
                        items.push_back(&item);
                        if (const auto *capture = std::get_if<CapturePattern>(&item.node))
                            only_wildcards &= !capture->target.binding;
                        else if (const auto *capture = std::get_if<StarPattern>(&item.node)) {
                            star = i;
                            star_wildcard = !capture->target.binding;
                            only_wildcards &= star_wildcard;
                        } else
                            only_wildcards = false;
                    }
                    if (!star || items.size() > 1)
                        pattern_length(items.size() - (star ? 1 : 0), !star, drop, span);
                    if (only_wildcards)
                        emit(Opcode::PopTop, span);
                    else if (star_wildcard) {
                        for (std::size_t i = 0; i < items.size(); ++i) {
                            const auto *wildcard = std::get_if<CapturePattern>(&items[i]->node);
                            if (i == *star || (wildcard && !wildcard->target.binding))
                                continue;
                            const auto captured = context.captures.size() - start;
                            emit(Opcode::Copy, span, static_cast<std::uint32_t>(captured + 1));
                            const auto number = std::to_string(i < *star ? i : items.size() - i);
                            if (i > *star)
                                emit(Opcode::GetLen, span);
                            literal({Literal::Kind::Integer, {number.begin(), number.end()}}, span);
                            if (i > *star)
                                emit(Opcode::Binary, span,
                                     static_cast<std::uint32_t>(BinaryOp::Subtract));
                            emit(Opcode::LoadItem, span);
                            pattern(*items[i], context,
                                    pattern_failure(context, captured + 1, failed, span));
                        }
                        pattern_remove_container(context.captures.size() - start, span);
                    } else {
                        if (star) {
                            if (*star > 65535 || items.size() - *star - 1 > 65535)
                                error(span,
                                      "too many expressions in star-unpacking sequence pattern");
                            emit(Opcode::UnpackEx, span,
                                 static_cast<std::uint32_t>(*star |
                                                            ((items.size() - *star - 1) << 16)));
                        } else
                            emit(Opcode::UnpackSequence, span,
                                 static_cast<std::uint32_t>(items.size()));
                        emit(Opcode::BuildTuple, span, static_cast<std::uint32_t>(items.size()));
                        pattern_tuple(items, context, failed, span, true);
                    }
                } else if constexpr (std::is_same_v<T, MappingPattern>) {
                    std::vector<PatternKey> constants;
                    for (const auto &[key, child] : node.items)
                        if (auto constant = pattern_key(*key)) {
                            for (const auto &previous : constants)
                                if (equal_pattern_keys(previous, *constant))
                                    error(key->span, "mapping pattern checks duplicate key");
                            constants.push_back(std::move(*constant));
                        }
                    emit(Opcode::MatchMapping, span);
                    const auto drop = pattern_failure(context, 1, failed, span);
                    pattern_test(drop, span);
                    if (!node.items.empty())
                        pattern_length(node.items.size(), false, drop, span);
                    std::vector<const Pattern *> items;
                    for (const auto &[key, child] : node.items) {
                        expression(*key);
                        items.push_back(child.get());
                    }
                    emit(Opcode::BuildTuple, span, static_cast<std::uint32_t>(items.size()));
                    emit(Opcode::MatchKeys, span);
                    const auto outer = pattern_failure(context, 2, failed, span);
                    pattern_not_none(context, outer, span);
                    pattern_tuple(items, context, outer, span);
                    if (node.rest.binding) {
                        const auto depth =
                            static_cast<std::uint32_t>(context.captures.size() - start + 2);
                        emit(Opcode::Copy, span, depth);
                        emit(Opcode::Copy, span, depth);
                        emit(Opcode::CopyDictWithoutKeys, span);
                        capture(node.rest);
                    }
                    pattern_remove_container(context.captures.size() - start, span);
                    pattern_remove_container(context.captures.size() - start, span);
                } else if constexpr (std::is_same_v<T, ClassPattern>) {
                    expression(*node.type);
                    std::vector<const Pattern *> items;
                    for (const auto &item : node.positional)
                        items.push_back(item.get());
                    for (const auto &[name, child] : node.keywords) {
                        literal({Literal::Kind::String, name}, span);
                        items.push_back(child.get());
                    }
                    emit(Opcode::BuildTuple, span,
                         static_cast<std::uint32_t>(node.keywords.size()));
                    emit(Opcode::MatchClass, span,
                         static_cast<std::uint32_t>(node.positional.size()));
                    pattern_not_none(context, failed, span);
                    pattern_tuple(items, context, failed, span);
                }
            },
            value.node);
    }
    void match_statement(const MatchStmt &node, Span span)
    {
        expression(*node.subject);
        const auto done = block();
        for (const auto &item : node.cases) {
            const auto next = block();
            PatternContext context;
            emit(Opcode::DupTop, item.span);
            pattern(*item.pattern, context, next);
            const auto matched = block();
            jump(matched, item.span);
            for (const auto &failure : context.failures) {
                select(failure.block);
                for (std::size_t i = 0; i < failure.pops; ++i)
                    emit(Opcode::PopTop, failure.span);
                jump(failure.target, failure.span);
            }
            select(matched);
            auto reversed = context.captures;
            std::reverse(reversed.begin(), reversed.end());
            pattern_permute(context.captures, reversed, item.span);
            for (const auto *capture : context.captures)
                name(std::get<NameExpr>(capture->binding->node).name, item.span, Opcode::StoreFast,
                     Opcode::StoreGlobal);
            if (item.guard) {
                const auto guarded = block();
                test(*item.guard, guarded, next);
                select(guarded);
            }
            emit(Opcode::PopTop, item.span);
            suite(item.body);
            if (current().exit == FlowBlock::Exit::Open)
                jump(done, item.span);
            select(next);
        }
        emit(Opcode::PopTop, span);
        jump(done, span);
        select(done);
    }
    void suite(const Suite &body)
    {
        for (std::size_t index = 0; index < body.size(); ++index) {
            const auto &statement = body[index];
            if (!index && statement.get() == first_statement_) {
                if (scope_ == 0) {
                    literal({Literal::Kind::String, *program_.graphs[scope_].docstring},
                            statement->span);
                    const auto &symbols = program_.scopes[scope_].symbols;
                    const auto explicit_global =
                        std::any_of(symbols.begin(), symbols.end(), [](const auto &item) {
                            return item.name == U"__doc__" &&
                                   item.classification == NameClass::GlobalExplicit;
                        });
                    emit(explicit_global ? Opcode::StoreGlobal : Opcode::StoreName, statement->span,
                         0, U"__doc__");
                }
                continue;
            }
            if (current().exit != FlowBlock::Exit::Open)
                select(block());
            const auto span = statement->span;
            std::visit(
                [&](const auto &node) {
                    using T = std::decay_t<decltype(node)>;
                    if constexpr (std::is_same_v<T, ExprStmt>) {
                        expression(*node.value);
                        emit(scope_ == 0 && program_.mode == InputMode::Single ? Opcode::PrintExpr
                                                                               : Opcode::PopTop,
                             span);
                    } else if constexpr (std::is_same_v<T, AnnAssignStmt>)
                        annotated_assignment(node, span);
                    else if constexpr (std::is_same_v<T, AssignStmt>) {
                        expression(*node.value);
                        for (std::size_t i = 0; i < node.targets.size(); ++i) {
                            if (i + 1 < node.targets.size())
                                emit(Opcode::DupTop, span);
                            store(*node.targets[i]);
                        }
                    } else if constexpr (std::is_same_v<T, DeleteStmt>) {
                        for (const auto &item : node.targets)
                            store(*item, true);
                    } else if constexpr (std::is_same_v<T, AugAssignStmt>) {
                        if (const auto *attribute =
                                std::get_if<AttributeExpr>(&node.target->node)) {
                            expression(*attribute->object);
                            emit(Opcode::DupTop, span);
                            emit(Opcode::LoadAttr, span, 0, attribute->name);
                        } else if (const auto *item =
                                       std::get_if<SubscriptExpr>(&node.target->node)) {
                            expression(*item->object);
                            expression(*item->key);
                            emit(Opcode::Copy, span, 2);
                            emit(Opcode::Copy, span, 2);
                            emit(Opcode::LoadItem, span);
                        } else
                            expression(*node.target);
                        expression(*node.value);
                        emit(Opcode::Inplace, span, static_cast<std::uint32_t>(node.op));
                        if (const auto *attribute = std::get_if<AttributeExpr>(&node.target->node))
                            emit(Opcode::StoreAttr, span, 0, attribute->name);
                        else if (std::holds_alternative<SubscriptExpr>(node.target->node))
                            emit(Opcode::StoreItem, span);
                        else
                            store(*node.target);
                    } else if constexpr (std::is_same_v<T, IfStmt>) {
                        const auto yes = block(), no = block(), done = block();
                        test(*node.test, yes, no);
                        select(yes);
                        suite(node.body);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(no);
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, WhileStmt>) {
                        const auto test = block(), body = block(), exhausted = block(),
                                   done = block();
                        const auto loop = region(RegionKind::Loop, span, test, done);
                        jump(test, span);
                        select(test);
                        this->test(*node.test, body, exhausted);
                        select(body);
                        loops_.push_back(loop);
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        select(exhausted);
                        current().exit = FlowBlock::Exit::Leave;
                        current().target = loop;
                        current().span = span;
                        select(block());
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, ForStmt>) {
                        expression(*node.iterable);
                        if (!node.iterator_ready)
                            emit(node.asynchronous ? Opcode::GetAiter : Opcode::GetIter, span);
                        const auto test = block(), body = block(), exhausted = block(),
                                   broken = block(), done = block();
                        const auto loop = region(RegionKind::Loop, span, test, broken);
                        jump(test, span);
                        select(test);
                        current().exit = node.asynchronous ? FlowBlock::Exit::AsyncIterNext
                                                           : FlowBlock::Exit::IterNext;
                        current().target = body;
                        current().alternative = exhausted;
                        current().span = span;
                        select(body);
                        store(*node.target);
                        loops_.push_back(loop);
                        suite(node.body);
                        loops_.pop_back();
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(test, span);
                        select(exhausted);
                        current().exit = FlowBlock::Exit::Leave;
                        current().target = loop;
                        current().span = span;
                        select(block());
                        emit(Opcode::PopTop, span);
                        suite(node.alternative);
                        if (current().exit == FlowBlock::Exit::Open)
                            jump(done, span);
                        select(broken);
                        emit(Opcode::PopTop, span);
                        jump(done, span);
                        select(done);
                    } else if constexpr (std::is_same_v<T, MatchStmt>) {
                        match_statement(node, span);
                    } else if constexpr (std::is_same_v<T, ReturnStmt>) {
                        if (!star_handlers_.empty())
                            error(span, "'break', 'continue' and 'return' cannot appear in an "
                                        "except* block");
                        if (node.value && program_.scopes[scope_].coroutine &&
                            program_.scopes[scope_].generator)
                            error(span, "'return' with value in async generator");
                        if (program_.scopes[scope_].kind != ScopeKind::Function &&
                            program_.mode != InputMode::Eval)
                            error(span, "'return' outside function");
                        if (node.value)
                            expression(*node.value);
                        else
                            none(span);
                        current().exit = FlowBlock::Exit::Return;
                        current().span = span;
                    } else if constexpr (std::is_same_v<T, RaiseStmt>) {
                        if (node.exception) {
                            expression(*node.exception);
                            if (node.cause)
                                expression(*node.cause);
                            emit(Opcode::Raise, span, node.cause ? 2 : 1);
                        } else
                            emit(Opcode::Reraise, span);
                        current().exit = FlowBlock::Exit::Stop;
                    } else if constexpr (std::is_same_v<T, TryStmt>) {
                        if (node.cleanup.empty())
                            try_except(node, span);
                        else {
                            const auto id = region(RegionKind::Finally, span);
                            try_except(node, span);
                            finally_handler(id, node.cleanup, span);
                        }
                    } else if constexpr (std::is_same_v<T, WithStmt>) {
                        std::vector<std::size_t> managers;
                        for (const auto &item : node.items) {
                            expression(*item.context);
                            managers.push_back(region(RegionKind::With, item.context->span));
                            program_.graphs[scope_].regions[managers.back()].asynchronous =
                                node.asynchronous;
                            if (item.target)
                                store(*item.target);
                            else
                                emit(Opcode::PopTop, span);
                        }
                        suite(node.body);
                        for (auto i = managers.size(); i-- > 0;) {
                            close_protected(managers[i], span);
                            select(block());
                        }
                    } else if constexpr (std::is_same_v<T, ControlStmt>) {
                        if (node == ControlStmt::Pass)
                            return;
                        if (!star_handlers_.empty() && loops_.size() <= star_handlers_.back())
                            error(span, "'break', 'continue' and 'return' cannot appear in an "
                                        "except* block");
                        if (loops_.empty())
                            error(span, "'break'/'continue' outside loop");
                        emit(node == ControlStmt::Break ? Opcode::BreakLoop : Opcode::ContinueLoop,
                             span, static_cast<std::uint32_t>(loops_.back()));
                        current().exit = FlowBlock::Exit::Stop;
                    } else if constexpr (std::is_same_v<T, FunctionStmt>) {
                        for (const auto &decorator : node.decorators)
                            expression(*decorator);
                        function_value(node, span);
                        for (std::size_t i = 0; i < node.decorators.size(); ++i)
                            emit(Opcode::Call, span, 1);
                        name(node.binding, span, Opcode::StoreFast, Opcode::StoreGlobal);
                    } else if constexpr (std::is_same_v<T, AssertStmt>) {
                        const auto done = block(), failed = block();
                        test(*node.test, done, failed);
                        select(failed);
                        emit(Opcode::LoadAssertionError, span);
                        if (node.message)
                            expression(*node.message);
                        emit(Opcode::Call, span, node.message ? 1 : 0);
                        emit(Opcode::RaiseInstance, span);
                        current().exit = FlowBlock::Exit::Stop;
                        select(done);
                    } else if constexpr (std::is_same_v<T, CollectionAddStmt>) {
                        expression(*node.collection);
                        if (node.key)
                            expression(*node.key);
                        expression(*node.value);
                        emit(node.kind == CollectionKind::Dict  ? Opcode::StoreItem
                             : node.kind == CollectionKind::Set ? Opcode::SetAdd
                                                                : Opcode::ListAppend,
                             span);
                        if (!node.key)
                            emit(Opcode::PopTop, span);
                    } else if constexpr (std::is_same_v<T, ImportStmt>) {
                        auto level = std::to_string(node.level);
                        if (node.module) {
                            literal({Literal::Kind::Integer,
                                     std::u32string(level.begin(), level.end())},
                                    span);
                            for (const auto &alias : node.names)
                                literal({Literal::Kind::String, alias.name}, alias.span);
                            emit(Opcode::BuildTuple, span,
                                 static_cast<std::uint32_t>(node.names.size()));
                            emit(Opcode::ImportName, span, 0,
                                 mangle_name(*node.module, node.private_name));
                            for (const auto &alias : node.names) {
                                if (alias.name == U"*") {
                                    emit(Opcode::ImportStar, alias.span);
                                    return;
                                }
                                emit(Opcode::ImportFrom, alias.span, 0,
                                     mangle_name(alias.name, node.private_name));
                                name(alias.binding, alias.span, Opcode::StoreFast,
                                     Opcode::StoreGlobal);
                            }
                            emit(Opcode::PopTop, span);
                        } else {
                            for (const auto &alias : node.names) {
                                literal({Literal::Kind::Integer, U"0"}, alias.span);
                                none(alias.span);
                                emit(Opcode::ImportName, alias.span, 0,
                                     mangle_name(alias.name, node.private_name));
                                auto dot = alias.name.find(U'.');
                                const bool traversed = alias.asname && dot != alias.name.npos;
                                if (traversed) {
                                    do {
                                        const auto begin = dot + 1;
                                        dot = alias.name.find(U'.', begin);
                                        emit(Opcode::ImportFrom, alias.span, 0,
                                             mangle_name(
                                                 alias.name.substr(begin, dot == alias.name.npos
                                                                              ? dot
                                                                              : dot - begin),
                                                 node.private_name));
                                        if (dot != alias.name.npos) {
                                            emit(Opcode::Swap, alias.span, 2);
                                            emit(Opcode::PopTop, alias.span);
                                        }
                                    } while (dot != alias.name.npos);
                                }
                                name(alias.binding, alias.span, Opcode::StoreFast,
                                     Opcode::StoreGlobal);
                                if (traversed)
                                    emit(Opcode::PopTop, alias.span);
                            }
                        }
                    } else if constexpr (std::is_same_v<T, ClassStmt>) {
                        nested(node.scope, node.body);
                        for (const auto &decorator : node.decorators)
                            expression(*decorator);
                        emit(Opcode::LoadBuildClass, span);
                        make_function(node.scope, span);
                        literal({Literal::Kind::String, node.name}, span);
                        const bool unpack =
                            std::any_of(node.bases.begin(), node.bases.end(),
                                        [](const auto &base) {
                                            return std::holds_alternative<StarredExpr>(base->node);
                                        }) ||
                            std::any_of(node.keywords.begin(), node.keywords.end(),
                                        [](const auto &keyword) { return keyword.first.empty(); });
                        if (unpack) {
                            emit(Opcode::BuildList, span, 2);
                            for (const auto &base : node.bases) {
                                if (const auto *star = std::get_if<StarredExpr>(&base->node)) {
                                    expression(*star->value);
                                    emit(Opcode::ListExtend, base->span);
                                } else {
                                    expression(*base);
                                    emit(Opcode::ListAppend, base->span);
                                }
                            }
                            emit(Opcode::ListToTuple, span);
                            emit(Opcode::BuildMap, span);
                            for (const auto &[name, item] : node.keywords) {
                                if (!name.empty())
                                    literal({Literal::Kind::String, name}, item->span);
                                expression(*item);
                                if (!name.empty())
                                    emit(Opcode::BuildMap, item->span, 1);
                                emit(Opcode::DictMerge, item->span);
                            }
                            emit(Opcode::CallEx, span);
                        } else {
                            for (const auto &base : node.bases)
                                expression(*base);
                            for (const auto &keyword : node.keywords)
                                expression(*keyword.second);
                            emit(Opcode::Call, span,
                                 static_cast<std::uint32_t>(node.bases.size() + 2));
                            for (const auto &keyword : node.keywords)
                                current().instructions.back().keywords.push_back(keyword.first);
                        }
                        for (std::size_t i = 0; i < node.decorators.size(); ++i)
                            emit(Opcode::Call, span, 1);
                        name(node.binding, span, Opcode::StoreFast, Opcode::StoreGlobal);
                    } else
                        static_assert(std::is_same_v<T, DeclareStmt>,
                                      "CFG lowering missing statement");
                },
                statement->node);
        }
    }
    void function(std::size_t scope, const Suite &body, const FunctionStmt *definition = nullptr)
    {
        scope_ = scope;
        select(block());
        loops_.clear();
        star_handlers_.clear();
        first_statement_ = nullptr;
        if (!body.empty() && !(scope == 0 && program_.mode == InputMode::Single))
            if (const auto *expression = std::get_if<ExprStmt>(&body.front()->node))
                if (const auto *literal = std::get_if<Literal>(&expression->value->node);
                    literal && literal->kind == Literal::Kind::String) {
                    program_.graphs[scope].docstring = literal->text;
                    first_statement_ = body.front().get();
                }
        if (definition) {
            auto &signature = program_.graphs[scope].signature;
            for (const auto &parameter : definition->parameters)
                signature.parameters.push_back({parameter.name, parameter.kind});
            signature.varargs = definition->varargs;
            signature.varkwargs = definition->varkwargs;
        }
        const auto &info = program_.scopes[scope];
        if (info.kind == ScopeKind::Class) {
            emit(Opcode::LoadName, info.span, 0, U"__name__");
            emit(Opcode::StoreName, info.span, 0, U"__module__");
            literal({Literal::Kind::String, info.qualname}, info.span);
            emit(Opcode::StoreName, info.span, 0, U"__qualname__");
        }
        if (info.annotations && info.kind != ScopeKind::Function)
            emit(Opcode::SetupAnnotations, info.span, 0, U"__annotations__");
        if (info.kind == ScopeKind::Class) {
            if (const auto &docstring = program_.graphs[scope].docstring) {
                literal({Literal::Kind::String, *docstring}, info.span);
                emit(Opcode::StoreName, info.span, 0, U"__doc__");
            }
        }
        suite(body);
        if (current().exit == FlowBlock::Exit::Open) {
            if (info.kind == ScopeKind::Class && !info.cellvars.empty()) {
                emit(Opcode::LoadClosure, info.span, 0);
                emit(Opcode::DupTop, info.span);
                emit(Opcode::StoreName, info.span, 0, U"__classcell__");
            } else
                none(info.span);
            current().exit = FlowBlock::Exit::Return;
            current().span = program_.scopes[scope].span;
        }
    }
    SourceProgram &program_;
    std::size_t scope_ = 0, block_ = 0;
    std::vector<std::size_t> loops_;
    std::vector<std::size_t> star_handlers_;
    const Stmt *first_statement_ = nullptr;
};

void literal(Attachment &context, const Literal &value, ReturnSlot &out)
{
    if (value.kind == Literal::Kind::Imaginary) {
        make_complex(context, 0.0, value.number, out);
        return;
    }
    if (value.kind == Literal::Kind::Bytes) {
        std::string data;
        data.reserve(value.text.size());
        for (auto ch : value.text)
            data.push_back(static_cast<char>(ch));
        make_bytes(context, std::move(data), out);
        return;
    }
    if (value.kind == Literal::Kind::Float) {
        make_float(context, value.number, out);
        return;
    }
    if (value.kind == Literal::Kind::String) {
        make_str(context, value.text, out);
        return;
    }
    if (value.kind != Literal::Kind::Integer) {
        out.ok(context.builtin(value.kind == Literal::Kind::Ellipsis ? CoreObject::Ellipsis
                               : value.kind == Literal::Kind::None   ? CoreObject::None
                               : value.kind == Literal::Kind::True   ? CoreObject::True
                                                                     : CoreObject::False));
        return;
    }
    boost::multiprecision::cpp_int number = 0;
    unsigned base = 10;
    std::size_t begin = 0;
    if (value.text.size() > 1 && value.text[0] == U'0') {
        const auto ch = value.text[1];
        base = ch == U'x' || ch == U'X'   ? 16
               : ch == U'o' || ch == U'O' ? 8
               : ch == U'b' || ch == U'B' ? 2
                                          : 10;
        if (base != 10)
            begin = 2;
    }
    for (auto i = begin; i < value.text.size(); ++i) {
        const auto ch = value.text[i];
        if (ch == U'_')
            continue;
        const auto digit = ch <= U'9' ? ch - U'0' : ch <= U'F' ? ch - U'A' + 10 : ch - U'a' + 10;
        number *= base;
        number += digit;
        if (i % 64 == 63)
            context.poll();
    }
    make_int_decimal(context, number.str(), out);
}
} // namespace
} // namespace cxxpython::core::detail

namespace cxxpython::core {
SourceUnit::SourceUnit(SourceBuffer source, InputMode mode, std::uint32_t future_flags)
{
    auto program = std::make_unique<detail::SourceProgram>(
        detail::SourceProgram{std::move(source), mode, {}, {}, future_flags});
    auto body = detail::parse_source(program->source, mode);
    bool prefix = true;
    for (std::size_t i = 0; i < body.size(); ++i) {
        if (!i)
            if (auto expr = std::get_if<detail::ExprStmt>(&body[i]->node))
                if (auto literal = std::get_if<detail::Literal>(&expr->value->node);
                    literal && literal->kind == detail::Literal::Kind::String)
                    continue;
        auto imported = std::get_if<detail::ImportStmt>(&body[i]->node);
        if (!prefix || !imported || imported->module != U"__future__" || imported->level) {
            prefix = false;
            continue;
        }
        imported->future = true;
        for (const auto &name : imported->names) {
            if (name.name == U"annotations")
                program->future_flags |= future_annotations;
            else if (name.name == U"barry_as_FLUFL")
                throw SourceError(program->source, name.span,
                                  "Barry comparison syntax is not implemented",
                                  SourceErrorKind::Capability);
            else if (name.name != U"nested_scopes" && name.name != U"generators" &&
                     name.name != U"division" && name.name != U"absolute_import" &&
                     name.name != U"with_statement" && name.name != U"print_function" &&
                     name.name != U"unicode_literals" && name.name != U"generator_stop")
                throw SourceError(program->source, name.span,
                                  name.name == U"braces"
                                      ? "not a chance"
                                      : "future feature " + source_utf8(name.name) +
                                            " is not defined");
        }
    }
    detail::analyze_scopes(*program, body);
    detail::GraphPass(*program).build(body);
    program_ = std::move(program);
}
SourceUnit::~SourceUnit() = default;
SourceUnit::SourceUnit(SourceUnit &&) noexcept = default;
SourceUnit &SourceUnit::operator=(SourceUnit &&) noexcept = default;
const SourceBuffer &SourceUnit::source() const noexcept
{
    return program_->source;
}
std::span<const ScopeInfo> SourceUnit::scopes() const noexcept
{
    return program_->scopes;
}
std::uint32_t SourceUnit::future_flags() const noexcept
{
    return program_->future_flags;
}
void SourceUnit::freeze(Attachment &context, ReturnSlot &out) const
{
    out.reset(context);
    try {
        RootScope roots(context);
        ReturnSlot temporary(context);
        std::vector<Handle> codes(program_->graphs.size());
        const auto filename_text = detail::decode_filename(source());
        for (std::size_t id = codes.size(); id-- > 0;) {
            const auto &graph = program_->graphs[id];
            // Local annotations are analyzed but never evaluated, so lambdas
            // and comprehensions inside them do not produce executable Code.
            if (graph.blocks.empty())
                continue;
            const auto &scope = program_->scopes[id];
            CodeSpec spec;
            spec.name = scope.name;
            spec.qualname = scope.qualname;
            spec.filename = filename_text;
            spec.first_line = source().position(scope.span).line;
            spec.locals = scope.locals;
            spec.signature = graph.signature;
            spec.docstring = graph.docstring;
            spec.cellvars = scope.cellvars;
            spec.freevars = scope.freevars;
            spec.generator = scope.generator;
            spec.coroutine = scope.coroutine;
            spec.future_flags = program_->future_flags;
            for (auto parent = scope.parent; parent; parent = program_->scopes[*parent].parent)
                spec.nested |= program_->scopes[*parent].kind == ScopeKind::Function;
            spec.kind = scope.kind == ScopeKind::Class    ? CodeKind::ClassBody
                        : scope.kind == ScopeKind::Module ? CodeKind::Module
                                                          : CodeKind::Function;
            CodeBuilder builder(context, std::move(spec));
            std::vector<CodeBuilder::Label> labels;
            for (std::size_t i = 0; i < graph.blocks.size(); ++i)
                labels.push_back(builder.label());
            std::vector<CodeBuilder::Region> regions;
            for (const auto &region : graph.regions)
                regions.push_back(region.kind == RegionKind::Loop
                                      ? builder.loop(labels[*region.continue_target],
                                                     labels[*region.break_target])
                                      : builder.region(region.kind, region.asynchronous));
            for (auto i : graph.order) {
                const auto &block = graph.blocks[i];
                if (block.handler)
                    builder.handler(regions[*block.handler]);
                builder.bind(labels[i]);
                for (const auto &instruction : block.instructions) {
                    const auto position = source().position(instruction.span);
                    auto operand = instruction.operand;
                    if (instruction.literal) {
                        detail::literal(context, *instruction.literal, temporary);
                        operand = builder.constant(roots.root(temporary.value()));
                    } else if (instruction.function)
                        operand = builder.constant(codes[*instruction.function]);
                    else if (opcode_definition(instruction.opcode).operand == OperandDomain::Name)
                        operand = builder.name(instruction.name);
                    if (instruction.opcode == Opcode::EnterRegion)
                        builder.enter(regions[operand], position);
                    else if (instruction.opcode == Opcode::BreakLoop ||
                             instruction.opcode == Opcode::ContinueLoop)
                        builder.control(instruction.opcode, regions[operand]);
                    else if (instruction.opcode == Opcode::Call)
                        builder.call(operand, instruction.keywords, position);
                    else if (instruction.opcode == Opcode::BuildNameDict)
                        builder.name_dict(instruction.keywords, position);
                    else
                        builder.emit(instruction.opcode, operand, position);
                }
                const auto position = source().position(block.span);
                if (block.exit == detail::FlowBlock::Exit::Return)
                    builder.emit(Opcode::Return, 0, position);
                else if (block.exit == detail::FlowBlock::Exit::Jump)
                    builder.jump(Opcode::Jump, labels[block.target], position);
                else if (block.exit == detail::FlowBlock::Exit::IterNext ||
                         block.exit == detail::FlowBlock::Exit::AsyncIterNext) {
                    builder.jump(block.exit == detail::FlowBlock::Exit::AsyncIterNext
                                     ? Opcode::ForAwait
                                     : Opcode::ForIter,
                                 labels[block.alternative], position);
                    builder.jump(Opcode::Jump, labels[block.target], position);
                } else if (block.exit == detail::FlowBlock::Exit::Branch) {
                    builder.jump(Opcode::JumpIfFalse, labels[block.alternative], position);
                    builder.jump(Opcode::Jump, labels[block.target], position);
                } else if (block.exit == detail::FlowBlock::Exit::Leave) {
                    if (const auto normal = graph.regions[block.target].normal_target)
                        builder.leave(regions[block.target], labels[*normal], position);
                    else
                        builder.leave(regions[block.target], position);
                } else if (block.exit == detail::FlowBlock::Exit::EndHandler)
                    builder.end_handler(regions[block.target]);
                else if (block.exit != detail::FlowBlock::Exit::Stop)
                    throw std::logic_error("compiler left a CFG block unterminated");
            }
            builder.freeze(temporary);
            codes[id] = roots.root(temporary.value());
        }
        out.ok(codes[0].get(context));
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
}
ResultView run(Attachment &context, const RunRequest &request, ReturnSlot &out)
{
    out.check_caller(context);
    RootScope roots(context);
    ReturnSlot compiled(context), environment(context), temporary(context);
    std::optional<Handle> input_code, input_globals, input_builtins, input_locals;
    if (const auto *code = std::get_if<Handle>(&request.program))
        input_code = roots.root(code->get(context));
    if (request.globals)
        input_globals = roots.root(request.globals->get(context));
    if (request.builtins)
        input_builtins = roots.root(request.builtins->get(context));
    if (request.locals)
        input_locals = roots.root(request.locals->get(context));
    out.reset(context);
    try {
        if (const auto *source = std::get_if<const SourceUnit *>(&request.program)) {
            if (!*source)
                throw std::invalid_argument("RunRequest source is null");
            (*source)->freeze(context, compiled);
            if (compiled.tag() == ResultTag::Raised) {
                out.raised(compiled.exception());
                return out.result();
            }
        } else
            compiled.ok(input_code->get(context));
        if (input_globals)
            environment.ok(input_globals->get(context));
        else
            context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                                  environment);
        const auto globals = roots.root(environment.value());
        auto dictionary = [&](Value value) {
            return context.has_layout<DictObject>(value) &&
                   is_subtype(context, context.type_of(value),
                              context.builtin(CoreObject::DictType));
        };
        if (!dictionary(globals.get(context))) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"RunRequest globals must be a name dictionary", out);
            return out.result();
        }
        auto builtins = roots.root(input_builtins ? input_builtins->get(context)
                                                  : context.builtin(CoreObject::Builtins));
        ReturnSlot builtin_key(context), supplied(context);
        make_str(context, U"__builtins__", builtin_key);
        const auto key = roots.root(builtin_key.value());
        dict_lookup(context, globals, key, supplied);
        if (supplied.tag() == ResultTag::Raised) {
            out.raised(supplied.exception());
            return out.result();
        }
        if (!input_builtins && supplied.tag() == ResultTag::Ok)
            builtins = roots.root(supplied.value());
        if (is_module(context, builtins.get(context)))
            builtins = roots.root(module_dict(context, builtins.get(context)));
        if (!dictionary(builtins.get(context))) {
            raise_error(context, CoreObject::TypeErrorType,
                        U"RunRequest requires globals/builtins name dictionaries", out);
            return out.result();
        }
        if (supplied.tag() == ResultTag::Empty) {
            dict_store(context, globals, key, builtins, out);
            if (out.tag() == ResultTag::Raised)
                return out.result();
        }
        if (!input_globals) {
            make_str(context, U"__name__", temporary);
            const auto name = roots.root(temporary.value());
            make_str(context, U"__main__", temporary);
            detail::name_store(context, globals.get(context), name.get(context), temporary.value());
        }
        make_function(context, {roots.root(compiled.value()), globals, {}, {}, builtins},
                      temporary);
        if (temporary.tag() == ResultTag::Raised)
            out.raised(temporary.exception());
        else if (input_locals)
            detail::execute_class_body(context, roots.root(temporary.value()), *input_locals, out);
        else
            call(context, roots.root(temporary.value()), {}, out);
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}
} // namespace cxxpython::core
