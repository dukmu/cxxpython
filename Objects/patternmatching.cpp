#include "internal/patternmatching.hpp"

#include "cxxpython/objects.hpp"
#include "cxxpython/protocol.hpp"

#include <array>

// Protocol order follows CPython 3.12.14 Python/ceval.c matching helpers;
// managed temporaries use this runtime's precise roots rather than refcounts.
namespace cxxpython::core {
namespace detail {
namespace {
bool tuple_argument(Attachment &c, Handle value, ReturnSlot &out)
{
    if (c.type_of(value.get(c)) == c.builtin(CoreObject::TupleType))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"pattern argument must be a tuple", out);
    return false;
}

bool attribute_error(Attachment &c, ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised &&
           is_subtype(c, c.type_of(out.exception()), c.builtin(CoreObject::AttributeErrorType));
}

std::u32string type_name(Attachment &c, Value type)
{
    TypeView view(c, type);
    auto name = view.name();
    if (auto text = std::get_if<std::u32string>(&name))
        return *text;
    return c.payload<StrObject>(std::get<Value>(name)).value;
}

void new_dict(Attachment &c, ReturnSlot &out)
{
    RootScope roots(c);
    c.allocate_instance<DictObject>(roots.root(c.builtin(CoreObject::DictType)), out);
}

bool remember(Attachment &c, Handle seen, Handle key, ReturnSlot &out)
{
    dict_lookup(c, seen, key, out);
    if (out.tag() == ResultTag::Raised)
        return false;
    if (out.tag() == ResultTag::Ok)
        return false;
    RootScope roots(c);
    dict_store(c, seen, key, roots.root(c.builtin(CoreObject::None)), out);
    return out.tag() != ResultTag::Raised;
}

bool self_match(Attachment &c, Handle cls)
{
    constexpr std::array types = {
        CoreObject::IntType,   CoreObject::BoolType,      CoreObject::StrType,
        CoreObject::BytesType, CoreObject::ByteArrayType, CoreObject::DictType,
        CoreObject::ListType,  CoreObject::SetType,       CoreObject::FrozenSetType,
        CoreObject::TupleType, CoreObject::FloatType};
    for (auto type : types)
        if (is_subtype(c, cls.get(c), c.builtin(type)))
            return true;
    return false;
}
} // namespace

void match_keys(Attachment &c, Handle subject, Handle keys_tuple, ReturnSlot &out)
{
    if (!tuple_argument(c, keys_tuple, out))
        return;
    const auto &keys = c.payload<TupleObject>(keys_tuple).items;
    if (keys.empty()) {
        make_tuple(c, {}, out);
        return;
    }
    RootScope roots(c);
    ReturnSlot work(c), method(c), sentinel(c), seen_slot(c);
    make_str(c, U"get", work);
    get_attribute(c, subject, roots.root(work.value()), method);
    if (method.tag() == ResultTag::Raised)
        return out.raised(method.exception());
    new_dict(c, seen_slot);
    if (seen_slot.tag() == ResultTag::Raised)
        return out.raised(seen_slot.exception());
    auto seen = roots.root(seen_slot.value());
    call(c, roots.root(c.object_type()), {}, sentinel);
    if (sentinel.tag() == ResultTag::Raised)
        return out.raised(sentinel.exception());
    std::vector<Value> values;
    for (Value value : keys) {
        auto key = roots.root(value);
        if (!remember(c, seen, key, work)) {
            if (work.tag() == ResultTag::Raised)
                return out.raised(work.exception());
            repr(c, key, work);
            if (work.tag() == ResultTag::Raised)
                return out.raised(work.exception());
            auto message = U"mapping pattern checks duplicate key (" +
                           c.payload<StrObject>(work.value()).value + U")";
            return raise_error(c, CoreObject::ValueErrorType, std::move(message), out);
        }
        std::array args{key.get(c), sentinel.value()};
        call(c, roots.root(method.value()), {args}, work);
        if (work.tag() == ResultTag::Raised)
            return out.raised(work.exception());
        if (work.value() == sentinel.value())
            return out.ok(c.builtin(CoreObject::None));
        values.push_back(roots.root(work.value()).get(c));
    }
    cxxpython::core::make_tuple(c, values, out);
}

void match_mapping_rest(Attachment &c, Handle subject, Handle keys_tuple, ReturnSlot &out)
{
    if (!tuple_argument(c, keys_tuple, out))
        return;
    RootScope roots(c);
    ReturnSlot copy(c), work(c);
    new_dict(c, copy);
    if (copy.tag() == ResultTag::Raised)
        return out.raised(copy.exception());
    auto dictionary = roots.root(copy.value());
    dict_merge(c, dictionary, subject, false, work);
    if (work.tag() == ResultTag::Raised)
        return out.raised(work.exception());
    for (auto key : c.payload<TupleObject>(keys_tuple).items) {
        auto rooted_key = roots.root(key);
        dict_erase(c, dictionary, rooted_key, work);
        if (work.tag() == ResultTag::Raised)
            return out.raised(work.exception());
        if (work.tag() == ResultTag::Empty) {
            std::array args{rooted_key.get(c)};
            call(c, roots.root(c.builtin(CoreObject::KeyErrorType)), {args}, work);
            if (work.tag() == ResultTag::Raised)
                return out.raised(work.exception());
            return out.raised(work.value());
        }
    }
    out.ok(dictionary.get(c));
}

void match_class(Attachment &c, Handle subject, Handle cls, Handle keyword_names_tuple,
                 std::size_t positional, ReturnSlot &out)
{
    if (!c.has_layout<TypeObject>(cls.get(c)))
        return raise_error(c, CoreObject::TypeErrorType, U"called match pattern must be a class",
                           out);
    if (!tuple_argument(c, keyword_names_tuple, out))
        return;
    RootScope roots(c);
    ReturnSlot work(c), seen_slot(c), match_args(c);
    instance_check(c, subject, cls, work);
    if (work.tag() == ResultTag::Raised)
        return out.raised(work.exception());
    truth(c, roots.root(work.value()), work);
    if (work.tag() == ResultTag::Raised)
        return out.raised(work.exception());
    if (work.value() == c.builtin(CoreObject::False))
        return out.ok(c.builtin(CoreObject::None));
    new_dict(c, seen_slot);
    if (seen_slot.tag() == ResultTag::Raised)
        return out.raised(seen_slot.exception());
    auto seen = roots.root(seen_slot.value());
    std::vector<Value> values;
    bool match_self = false;
    if (positional) {
        make_str(c, U"__match_args__", work);
        get_attribute(c, cls, roots.root(work.value()), match_args);
        if (attribute_error(c, match_args)) {
            match_self = self_match(c, cls);
            make_tuple(c, {}, match_args);
            if (match_args.tag() == ResultTag::Raised)
                return out.raised(match_args.exception());
        } else if (match_args.tag() == ResultTag::Raised) {
            return out.raised(match_args.exception());
        }
        if (c.type_of(match_args.value()) != c.builtin(CoreObject::TupleType))
            return raise_error(c, CoreObject::TypeErrorType,
                               type_name(c, cls.get(c)) + U".__match_args__ must be a tuple (got " +
                                   type_name(c, c.type_of(match_args.value())) + U")",
                               out);
        auto allowed = match_self ? 1 : c.payload<TupleObject>(match_args.value()).items.size();
        if (positional > allowed) {
            auto number = [](std::size_t n) {
                auto s = std::to_string(n);
                return std::u32string(s.begin(), s.end());
            };
            return raise_error(c, CoreObject::TypeErrorType,
                               type_name(c, cls.get(c)) + U"() accepts " + number(allowed) +
                                   U" positional sub-pattern" + (allowed == 1 ? U"" : U"s") +
                                   U" (" + number(positional) + U" given)",
                               out);
        }
        if (match_self)
            values.push_back(subject.get(c));
    }
    auto extract = [&](Value name) -> bool {
        auto key = roots.root(name);
        if (c.type_of(name) != c.builtin(CoreObject::StrType)) {
            raise_error(c, CoreObject::TypeErrorType,
                        U"__match_args__ elements must be strings (got " +
                            type_name(c, c.type_of(name)) + U")",
                        out);
            return false;
        }
        if (!remember(c, seen, key, work)) {
            if (work.tag() == ResultTag::Raised) {
                out.raised(work.exception());
                return false;
            }
            repr(c, key, work);
            if (work.tag() == ResultTag::Raised) {
                out.raised(work.exception());
                return false;
            }
            raise_error(c, CoreObject::TypeErrorType,
                        type_name(c, cls.get(c)) + U"() got multiple sub-patterns for attribute " +
                            c.payload<StrObject>(work.value()).value,
                        out);
            return false;
        }
        get_attribute(c, subject, key, work);
        if (attribute_error(c, work)) {
            out.ok(c.builtin(CoreObject::None));
            return false;
        }
        if (work.tag() == ResultTag::Raised) {
            out.raised(work.exception());
            return false;
        }
        values.push_back(roots.root(work.value()).get(c));
        return true;
    };
    if (positional && !match_self)
        for (std::size_t i = 0; i < positional; ++i)
            if (!extract(c.payload<TupleObject>(match_args.value()).items[i]))
                return;
    for (auto name : c.payload<TupleObject>(keyword_names_tuple).items)
        if (!extract(name))
            return;
    cxxpython::core::make_tuple(c, values, out);
}
} // namespace detail
} // namespace cxxpython::core
