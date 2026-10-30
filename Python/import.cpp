#include "internal/import.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/compiler.hpp"

#include <filesystem>
#include <fstream>
#include <iterator>
#include <tuple>

namespace cxxpython::core {
namespace {
using detail::ImportState;
using detail::ModuleDefinition;
using detail::ModuleLoad;

ResultTag lookup_name(Attachment &context, Value dictionary, std::u32string name, ReturnSlot &out)
{
    RootScope roots(context);
    const auto mapping = roots.root(dictionary);
    ReturnSlot key(context);
    make_str(context, std::move(name), key);
    dict_lookup(context, mapping, roots.root(key.value()), out);
    return out.tag();
}
ResultTag store_name(Attachment &context, Value dictionary, std::u32string name, Value value,
                     ReturnSlot &out)
{
    RootScope roots(context);
    roots.root(dictionary);
    roots.root(value);
    ReturnSlot key(context);
    make_str(context, std::move(name), key);
    dict_store(context, roots.root(dictionary), roots.root(key.value()), roots.root(value), out);
    return out.tag();
}
void missing(Attachment &context, const std::u32string &name, ReturnSlot &out)
{
    raise_error(context, CoreObject::ModuleNotFoundErrorType, U"No module named '" + name + U"'",
                out);
    const auto error = out.exception();
    if (context.type_of(error) != context.builtin(CoreObject::ModuleNotFoundErrorType))
        return;
    RootScope roots(context);
    auto rooted = roots.root(error);
    ReturnSlot key(context), value(context), changed(context);
    make_str(context, U"name", key);
    make_str(context, name, value);
    set_attribute(context, rooted, roots.root(key.value()), roots.root(value.value()), changed);
    if (changed.tag() == ResultTag::Raised)
        out.raised(changed.exception());
    else {
        make_str(context, U"path", key);
        set_attribute(context, rooted, roots.root(key.value()),
                      roots.root(context.builtin(CoreObject::None)), changed);
        if (changed.tag() == ResultTag::Raised)
            out.raised(changed.exception());
    }
}
bool is_error(Attachment &context, ReturnSlot &out, CoreObject type)
{
    return out.tag() == ResultTag::Raised &&
           is_subtype(context, context.type_of(out.exception()), context.builtin(type));
}

// All host coordination runs parked. The lock is released before resuming;
// no Python reference is inspected and no managed operation runs in this scope.
class LoadLease final {
  public:
    LoadLease(Attachment &context, const std::u32string &name, std::shared_ptr<ModuleLoad> record)
        : context_(context), state_(context.imports()), name_(name), record_(std::move(record))
    {}
    ~LoadLease()
    {
        decltype(ImportState::loading)::node_type removed;
        {
            NativeWait parked(context_);
            std::lock_guard lock(state_.mutex);
            auto found = state_.loading.find(name_);
            if (found != state_.loading.end() && found->second == record_) {
                record_->complete = true;
                removed = state_.loading.extract(found);
                state_.changed.notify_all();
            }
        }
    }

  private:
    Attachment &context_;
    ImportState &state_;
    const std::u32string &name_;
    std::shared_ptr<ModuleLoad> record_;
};

void builtin_init(Attachment &context, Handle, ReturnSlot &out)
{
    out.ok(context.builtin(CoreObject::None));
}
void sys_init(Attachment &context, Handle module, ReturnSlot &out)
{
    auto dictionary = module_dict(context, module.get(context));
    if (store_name(context, dictionary, U"modules", context.builtin(CoreObject::Modules), out) ==
        ResultTag::Raised)
        return;
    RootScope roots(context);
    ReturnSlot temporary(context);
    make_str(context, U"", temporary);
    std::array paths{temporary.value()};
    make_list(context, paths, temporary);
    if (store_name(context, dictionary, U"path", temporary.value(), out) == ResultTag::Raised)
        return;
    make_list(context, {}, temporary);
    store_name(context, dictionary, U"argv", temporary.value(), out);
}
void main_init(Attachment &context, Handle module, ReturnSlot &out)
{
    if (store_name(context, module_dict(context, module.get(context)), U"__package__",
                   context.builtin(CoreObject::None), out) == ResultTag::Raised)
        return;
    import_module(context, U"builtins", out);
    if (out.tag() == ResultTag::Raised)
        return;
    if (store_name(context, module_dict(context, module.get(context)), U"__builtins__", out.value(),
                   out) == ResultTag::Raised)
        return;
    import_module(context, U"sys", out);
    if (out.tag() != ResultTag::Raised)
        out.ok(context.builtin(CoreObject::None));
}

std::shared_ptr<const ModuleDefinition> find_definition(Attachment &context,
                                                        const std::u32string &name)
{
    auto &state = context.imports();
    NativeWait parked(context);
    std::lock_guard lock(state.mutex);
    auto found = state.definitions.find(name);
    return found == state.definitions.end() ? nullptr : found->second;
}

std::shared_ptr<const ModuleDefinition> find_file(Attachment &context, const std::u32string &name,
                                                  ReturnSlot &out)
{
    RootScope roots(context);
    const auto dot = name.rfind(U'.');
    const auto owner_name = dot == std::u32string::npos ? U"sys" : name.substr(0, dot);
    lookup_name(context, context.builtin(CoreObject::Modules), owner_name, out);
    if (out.tag() == ResultTag::Raised)
        return nullptr;
    if (out.tag() == ResultTag::Empty && dot == std::u32string::npos) {
        import_module(context, U"sys", out);
        if (out.tag() == ResultTag::Raised)
            return nullptr;
    }
    if (out.tag() == ResultTag::Empty || !is_module(context, out.value()))
        return nullptr;
    const auto module = roots.root(out.value());
    lookup_name(context, module_dict(context, module.get(context)),
                dot == std::u32string::npos ? U"path" : U"__path__", out);
    if (out.tag() != ResultTag::Ok)
        return nullptr;
    const auto rooted_path = roots.root(out.value());
    ReturnSlot item(context);
    std::vector<std::string> paths;
    auto append_path = [&](Value value) {
        if (context.has_layout<StrObject>(value) &&
            is_subtype(context, context.type_of(value), context.builtin(CoreObject::StrType)))
            paths.push_back(source_utf8(context.payload<StrObject>(value).value));
    };
    if (context.type_of(rooted_path) == context.builtin(CoreObject::ListType)) {
        std::vector<Value> entries;
        detail::list_snapshot(context, rooted_path, roots, entries);
        for (auto value : entries)
            append_path(value);
    } else {
        get_iter(context, rooted_path, item);
        if (item.tag() == ResultTag::Raised) {
            out.raised(item.exception());
            return nullptr;
        }
        const auto iterator = roots.root(item.value());
        for (;;) {
            const auto status = iter_next(context, iterator, item);
            if (status == NextStatus::Done)
                break;
            if (status == NextStatus::Raised) {
                out.raised(item.exception());
                return nullptr;
            }
            append_path(item.value());
        }
    }
    const auto leaf = source_utf8(dot == std::u32string::npos ? name : name.substr(dot + 1));
    std::shared_ptr<const ModuleDefinition> result;
    std::string native_path;
    bool native_package = false;
    try {
        NativeWait parked(context);
        std::vector<std::string> portions;
        for (const auto &entry : paths) {
            const auto base = std::filesystem::absolute(entry.empty() ? "." : entry);
            std::error_code error;
            std::filesystem::path selected;
            bool is_package = false, native = false;
            for (auto [candidate, package, extension] :
                 {std::tuple{base / leaf / "__init__.cxxpy.so", true, true},
                  {base / leaf / "__init__.py", true, false},
                  {base / (leaf + ".cxxpy.so"), false, true},
                  {base / (leaf + ".py"), false, false}}) {
                if (std::filesystem::is_regular_file(candidate, error)) {
                    selected = std::move(candidate);
                    is_package = package;
                    native = extension;
                    break;
                }
            }
            if (selected.empty()) {
                const auto directory = base / leaf;
                if (std::filesystem::is_directory(directory, error))
                    portions.push_back(directory.string());
                continue;
            }
            if (native) {
                native_path = selected.string();
                native_package = is_package;
                break;
            }
            std::ifstream stream(selected, std::ios::binary);
            if (!stream)
                throw std::ios_base::failure("cannot open module source");
            std::string text{std::istreambuf_iterator<char>(stream),
                             std::istreambuf_iterator<char>()};
            if (stream.bad())
                throw std::ios_base::failure("cannot read module source");
            const auto origin = selected.string();
            auto source = std::make_shared<const SourceUnit>(SourceBuffer(std::move(text), origin));
            result = std::make_shared<const ModuleDefinition>(ModuleDefinition{
                std::move(source), is_package, origin,
                is_package ? std::vector<std::string>{selected.parent_path().string()}
                           : std::vector<std::string>{}});
            break;
        }
        if (!result && native_path.empty() && !portions.empty()) {
            auto source = std::make_shared<const SourceUnit>(SourceBuffer(""));
            result = std::make_shared<const ModuleDefinition>(
                ModuleDefinition{std::move(source), true, "", std::move(portions)});
        }
    } catch (const SourceError &error) {
        const auto type = error.kind == SourceErrorKind::Indentation
                              ? CoreObject::IndentationErrorType
                          : error.kind == SourceErrorKind::Tab ? CoreObject::TabErrorType
                                                               : CoreObject::SyntaxErrorType;
        raise_error(context, type, detail::decode_filename(SourceBuffer("", error.what())), out);
    } catch (const std::ios_base::failure &) {
        raise_error(context, CoreObject::OSErrorType, U"cannot read module source", out);
    } catch (const std::filesystem::filesystem_error &) {
        raise_error(context, CoreObject::OSErrorType, U"cannot access module source", out);
    }
    if (!native_path.empty()) {
        auto native = detail::load_extension(context, native_path, name, out);
        if (native)
            result = std::make_shared<const ModuleDefinition>(ModuleDefinition{
                std::move(native), native_package, native_path,
                native_package ? std::vector<std::string>{std::filesystem::path(native_path)
                                                              .parent_path()
                                                              .string()}
                               : std::vector<std::string>{}});
    }
    return result;
}

void builtin_import(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(context);
    import_name(context, roots.root(args[0]), roots.root(args[1]), roots.root(args[3]),
                roots.root(args[4]), out);
}
template <std::size_t Index>
void import_error_get(Attachment &context, Value error, ReturnSlot &out)
{
    context.load(error, context.instance_slot(error, Index), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
}
template <std::size_t Index>
void import_error_set(Attachment &context, Value error, std::optional<Value> value, ReturnSlot &out)
{
    context.store(error, context.instance_slot(error, Index), value);
    out.ok(context.builtin(CoreObject::None));
}
} // namespace

void detail::register_definition(Attachment &context, std::u32string name,
                                 std::shared_ptr<const ModuleDefinition> definition)
{
    if (name.empty())
        throw std::invalid_argument("module registration requires a name");
    std::map<std::u32string, std::shared_ptr<const ModuleDefinition>> prepared;
    prepared.emplace(std::move(name), std::move(definition));
    auto node = prepared.extract(prepared.begin());
    decltype(prepared)::node_type previous;
    auto &state = context.imports();
    {
        NativeWait parked(context);
        std::lock_guard lock(state.mutex);
        previous = state.definitions.extract(node.key());
        state.definitions.insert(std::move(node));
    }
}

detail::ImportState::ImportState()
{
    for (auto [name, initializer] :
         {std::pair{U"builtins", builtin_init}, {U"sys", sys_init}, {U"__main__", main_init}})
        definitions.emplace(name, std::make_shared<const ModuleDefinition>(
                                      ModuleDefinition{initializer, false, "built-in", {}}));
}

void register_source_module(Attachment &context, std::u32string name,
                            std::shared_ptr<const SourceUnit> source, bool package)
{
    if (!source)
        throw std::invalid_argument("source module requires source");
    const auto origin = source->source().filename();
    detail::register_definition(context, std::move(name),
                                std::make_shared<const ModuleDefinition>(
                                    ModuleDefinition{std::move(source), package, origin, {}}));
}
void register_native_module(Attachment &context, std::u32string name, ModuleInitializer entry)
{
    if (!entry)
        throw std::invalid_argument("native module requires an initializer");
    detail::register_definition(
        context, std::move(name),
        std::make_shared<const ModuleDefinition>(ModuleDefinition{entry, false, "built-in", {}}));
}

ResultView import_module(Attachment &context, const std::u32string &name, ReturnSlot &out)
{
    out.reset(context);
    RootScope roots(context);
    const auto modules = roots.root(context.builtin(CoreObject::Modules));
    auto &state = context.imports();
    try {
        if (name.empty()) {
            raise_error(context, CoreObject::ValueErrorType, U"Empty module name", out);
            return out.result();
        }
        ReturnSlot key(context);
        make_str(context, name, key);
        const auto module_key = roots.root(key.value());
        std::map<std::u32string, std::shared_ptr<ModuleLoad>> prepared;
        const auto record = std::make_shared<ModuleLoad>();
        prepared.emplace(name, record);
        auto node = prepared.extract(prepared.begin());
        std::map<std::thread::id, std::shared_ptr<ModuleLoad>> wait_nodes;
        wait_nodes.emplace(record->owner, nullptr);
        auto wait_node = wait_nodes.extract(wait_nodes.begin());
        bool partial = false;
        for (;;) {
            bool acquired = false;
            {
                NativeWait parked(context);
                std::unique_lock lock(state.mutex);
                auto existing = state.loading.find(name);
                if (existing == state.loading.end()) {
                    state.loading.insert(std::move(node));
                    acquired = true;
                } else {
                    auto pending = existing->second;
                    auto owner = pending->owner;
                    partial = owner == record->owner;
                    for (std::size_t i = 0; !partial && i < state.waiting.size(); ++i) {
                        auto waiting = state.waiting.find(owner);
                        if (waiting == state.waiting.end())
                            break;
                        owner = waiting->second->owner;
                        partial = owner == record->owner;
                    }
                    if (!partial) {
                        wait_node.mapped() = pending;
                        state.waiting.insert(std::move(wait_node));
                        state.changed.wait(lock, [&] { return pending->complete; });
                        wait_node = state.waiting.extract(record->owner);
                    }
                }
            }
            if (acquired || partial)
                break;
        }
        if (partial) {
            dict_lookup(context, modules, module_key, out);
            if (out.tag() == ResultTag::Empty ||
                (out.tag() == ResultTag::Ok && out.value() == context.builtin(CoreObject::None)))
                missing(context, name, out);
            return out.result();
        }
        LoadLease lease(context, name, record);
        dict_lookup(context, modules, module_key, out);
        if (out.tag() != ResultTag::Empty) {
            if (out.tag() == ResultTag::Ok && out.value() == context.builtin(CoreObject::None))
                missing(context, name, out);
            return out.result();
        }
        auto definition = find_definition(context, name);
        if (!definition) {
            definition = find_file(context, name, out);
            if (out.tag() == ResultTag::Raised)
                return out.result();
        }
        if (!definition) {
            missing(context, name, out);
            return out.result();
        }
        make_module(context, name, out,
                    name == U"builtins"
                        ? std::optional<Handle>{roots.root(context.builtin(CoreObject::Builtins))}
                        : std::optional<Handle>{});
        if (out.tag() == ResultTag::Raised)
            return out.result();
        const auto module = roots.root(out.value());
        const auto dictionary = roots.root(module_dict(context, module.get(context)));
        ReturnSlot temporary(context);
        const auto dot = name.rfind(U'.');
        make_str(context,
                 definition->package           ? name
                 : dot == std::u32string::npos ? U""
                                               : name.substr(0, dot),
                 temporary);
        if (store_name(context, dictionary.get(context), U"__package__", temporary.value(), out) ==
            ResultTag::Raised)
            return out.result();
        if (store_name(context, dictionary.get(context), U"__builtins__",
                       context.builtin(CoreObject::Builtins), out) == ResultTag::Raised)
            return out.result();
        if (definition->package) {
            std::vector<Value> paths;
            for (const auto &path : definition->paths) {
                make_str(context, detail::decode_filename(SourceBuffer("", path)), temporary);
                paths.push_back(roots.root(temporary.value()).get(context));
            }
            make_list(context, paths, temporary);
            if (store_name(context, dictionary.get(context), U"__path__", temporary.value(), out) ==
                ResultTag::Raised)
                return out.result();
        }
        if (!definition->origin.empty() && definition->origin != "built-in") {
            make_str(context, detail::decode_filename(SourceBuffer("", definition->origin)),
                     temporary);
            if (store_name(context, dictionary.get(context), U"__file__", temporary.value(), out) ==
                ResultTag::Raised)
                return out.result();
        }
        detail::DictBinding binding;
        detail::dict_store_binding(context, modules, module_key, module, binding, out);
        if (out.tag() == ResultTag::Raised)
            return out.result();
        out.reset(context);
        try {
            if (auto source = std::get_if<std::shared_ptr<const SourceUnit>>(&definition->loader))
                run(context, {source->get(), dictionary}, out);
            else {
                if (const auto native = std::get_if<std::shared_ptr<const detail::NativeModule>>(
                        &definition->loader))
                    detail::exec_extension(context, *native, module, out);
                else
                    std::get<ModuleInitializer>(definition->loader)(context, module, out);
                if (out.tag() == ResultTag::Empty)
                    raise_error(context, CoreObject::RuntimeErrorType,
                                U"module initializer did not publish a result", out);
                else if (out.tag() == ResultTag::Ok &&
                         out.value() != context.builtin(CoreObject::None))
                    raise_error(context, CoreObject::TypeErrorType,
                                U"module initializer must return None", out);
            }
        } catch (...) {
            detail::dict_erase_binding(context, modules, binding, module);
            throw;
        }
        if (out.tag() == ResultTag::Raised) {
            detail::dict_erase_binding(context, modules, binding, module);
            return out.result();
        }
        dict_lookup(context, modules, module_key, out);
        if (out.tag() == ResultTag::Empty) {
            raise_error(context, CoreObject::KeyErrorType, name, out);
            return out.result();
        }
    } catch (const std::bad_alloc &) {
        out.raised(context.builtin(CoreObject::MemoryError));
    }
    return out.result();
}

ResultView import_name(Attachment &context, Handle name, Handle globals, Handle fromlist,
                       Handle level, ReturnSlot &out)
{
    RootScope roots(context);
    name = roots.root(name.get(context));
    globals = roots.root(globals.get(context));
    fromlist = roots.root(fromlist.get(context));
    level = roots.root(level.get(context));
    if (!context.has_layout<StrObject>(name.get(context)) ||
        !is_subtype(context, context.type_of(name), context.builtin(CoreObject::StrType))) {
        raise_error(context, CoreObject::TypeErrorType, U"module name must be a string", out);
        return out.result();
    }
    const auto text = context.payload<StrObject>(name).value;
    std::int64_t relative;
    ReturnSlot number(context);
    index(context, level, number);
    if (number.tag() == ResultTag::Raised) {
        out.raised(number.exception());
        return out.result();
    }
    if (!int_fits_signed(context, number.value(), relative)) {
        raise_error(context, CoreObject::OverflowErrorType, U"import level is too large", out);
        return out.result();
    }
    if (relative < 0) {
        raise_error(context, CoreObject::ValueErrorType, U"level must be nonnegative", out);
        return out.result();
    }
    std::u32string base;
    if (relative) {
        if (!context.has_layout<DictObject>(globals.get(context))) {
            raise_error(context, CoreObject::ImportErrorType,
                        U"relative import requires package globals", out);
            return out.result();
        }
        lookup_name(context, globals.get(context), U"__package__", out);
        if (out.tag() == ResultTag::Raised)
            return out.result();
        const bool fallback =
            out.tag() == ResultTag::Empty || out.value() == context.builtin(CoreObject::None);
        if (fallback)
            lookup_name(context, globals.get(context), U"__name__", out);
        if (out.tag() == ResultTag::Raised)
            return out.result();
        if (out.tag() != ResultTag::Ok || !context.has_layout<StrObject>(out.value())) {
            raise_error(context, CoreObject::ImportErrorType,
                        U"relative import has no known parent package", out);
            return out.result();
        }
        base = context.payload<StrObject>(out.value()).value;
        if (fallback) {
            lookup_name(context, globals.get(context), U"__path__", out);
            if (out.tag() == ResultTag::Raised)
                return out.result();
            if (out.tag() == ResultTag::Empty) {
                const auto dot = base.rfind(U'.');
                base.resize(dot == std::u32string::npos ? 0 : dot);
            }
        }
        for (std::int64_t i = 1; i < relative; ++i) {
            const auto dot = base.rfind(U'.');
            if (dot == std::u32string::npos) {
                raise_error(context, CoreObject::ImportErrorType,
                            U"relative import beyond top-level package", out);
                return out.result();
            }
            base.resize(dot);
        }
        if (base.empty()) {
            raise_error(context, CoreObject::ImportErrorType,
                        U"relative import has no known parent package", out);
            return out.result();
        }
    }
    const std::u32string absolute = base.empty() ? text : text.empty() ? base : base + U"." + text;
    const auto first_dot = text.find(U'.');
    const auto top_size =
        relative
            ? (first_dot == std::u32string::npos ? absolute.size() : base.size() + 1 + first_dot)
            : absolute.find(U'.');
    std::optional<Handle> parent, top;
    ReturnSlot key(context), item(context);
    std::size_t end = 0, begin = 0;
    do {
        end = absolute.find(U'.', begin);
        const auto prefix = absolute.substr(0, end);
        if (parent) {
            if (is_module(context, parent->get(context)))
                lookup_name(context, module_dict(context, parent->get(context)), U"__path__", out);
            else
                out.reset(context);
            if (out.tag() == ResultTag::Raised)
                return out.result();
            if (out.tag() == ResultTag::Empty) {
                missing(context, absolute, out);
                return out.result();
            }
        }
        import_module(context, prefix, out);
        if (out.tag() == ResultTag::Raised)
            return out.result();
        const auto current = roots.root(out.value());
        if (parent) {
            make_str(context,
                     absolute.substr(begin, end == std::u32string::npos ? end : end - begin), key);
            set_attribute(context, *parent, roots.root(key.value()), current, item);
            if (item.tag() == ResultTag::Raised) {
                out.raised(item.exception());
                return out.result();
            }
        }
        if (!top && (end == std::u32string::npos || end >= top_size))
            top = current;
        parent = current;
        begin = end + 1;
    } while (end != std::u32string::npos);
    const auto result = *parent;
    truth(context, fromlist, item);
    if (item.tag() == ResultTag::Raised) {
        out.raised(item.exception());
        return out.result();
    }
    if (item.value() == context.builtin(CoreObject::False)) {
        out.ok(top->get(context));
        return out.result();
    }
    if (is_module(context, result.get(context)))
        lookup_name(context, module_dict(context, result.get(context)), U"__path__", out);
    else
        out.reset(context);
    if (out.tag() == ResultTag::Raised)
        return out.result();
    if (out.tag() == ResultTag::Ok) {
        get_iter(context, fromlist, item);
        if (item.tag() == ResultTag::Raised) {
            out.raised(item.exception());
            return out.result();
        }
        std::vector<Handle> iterators{roots.root(item.value())};
        for (;;) {
            const auto status = iter_next(context, iterators.back(), item);
            if (status == NextStatus::Done) {
                iterators.pop_back();
                if (iterators.empty())
                    break;
                continue;
            }
            if (status == NextStatus::Raised) {
                out.raised(item.exception());
                return out.result();
            }
            if (!context.has_layout<StrObject>(item.value())) {
                raise_error(context, CoreObject::TypeErrorType, U"fromlist items must be strings",
                            out);
                return out.result();
            }
            const auto field = roots.root(item.value());
            const auto label = context.payload<StrObject>(field).value;
            if (label == U"*") {
                if (iterators.size() == 1) {
                    make_str(context, U"__all__", key);
                    get_attribute(context, result, roots.root(key.value()), out);
                    if (is_error(context, out, CoreObject::AttributeErrorType))
                        continue;
                    if (out.tag() == ResultTag::Raised)
                        return out.result();
                    get_iter(context, roots.root(out.value()), item);
                    if (item.tag() == ResultTag::Raised) {
                        out.raised(item.exception());
                        return out.result();
                    }
                    iterators.push_back(roots.root(item.value()));
                }
                continue;
            }
            get_attribute(context, result, field, out);
            if (out.tag() == ResultTag::Ok)
                continue;
            if (!is_error(context, out, CoreObject::AttributeErrorType))
                return out.result();
            const auto full = absolute + U"." + label;
            import_module(context, full, out);
            if (is_error(context, out, CoreObject::ModuleNotFoundErrorType)) {
                context.load(out.exception(), context.instance_slot(out.exception(), 0), item);
                if (item.tag() == ResultTag::Ok &&
                    context.payload<StrObject>(item.value()).value == full)
                    continue;
            }
            if (out.tag() == ResultTag::Raised)
                return out.result();
            set_attribute(context, result, field, roots.root(out.value()), item);
            if (item.tag() == ResultTag::Raised) {
                out.raised(item.exception());
                return out.result();
            }
        }
    }
    out.ok(result.get(context));
    return out.result();
}
ResultView import_from(Attachment &context, Handle module, Handle name, ReturnSlot &out)
{
    RootScope roots(context);
    module = roots.root(module.get(context));
    name = roots.root(name.get(context));
    get_attribute(context, module, name, out);
    if (is_error(context, out, CoreObject::AttributeErrorType)) {
        if (is_module(context, module.get(context))) {
            lookup_name(context, module_dict(context, module.get(context)), U"__name__", out);
            if (out.tag() == ResultTag::Raised)
                return out.result();
            if (out.tag() == ResultTag::Ok && context.has_layout<StrObject>(out.value())) {
                const auto full = context.payload<StrObject>(out.value()).value + U"." +
                                  context.payload<StrObject>(name).value;
                lookup_name(context, context.builtin(CoreObject::Modules), full, out);
                if (out.tag() != ResultTag::Empty) {
                    return out.result();
                }
            }
        }
        raise_error(context, CoreObject::ImportErrorType,
                    U"cannot import name '" + context.payload<StrObject>(name).value + U"'", out);
    }
    return out.result();
}
void set_import_path(Attachment &context, std::span<const std::string> paths, ReturnSlot &out)
{
    RootScope roots(context);
    import_module(context, U"sys", out);
    if (out.tag() == ResultTag::Raised)
        return;
    const auto module = roots.root(out.value());
    std::vector<Value> values;
    for (const auto &path : paths) {
        make_str(context, detail::decode_filename(SourceBuffer("", path)), out);
        values.push_back(roots.root(out.value()).get(context));
    }
    make_list(context, values, out);
    store_name(context, module_dict(context, module.get(context)), U"path", out.value(), out);
}
void detail::install_import_builtins(Attachment &context, std::span<const Handle> core,
                                     Value mapping)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    install_native_field(context, core, at(CoreObject::ImportErrorType), U"name",
                         import_error_get<0>, import_error_set<0>);
    install_native_field(context, core, at(CoreObject::ImportErrorType), U"path",
                         import_error_get<1>, import_error_set<1>);
    SignatureLayout signature;
    signature.parameters = {
        {U"name", ParameterKind::PositionalOrKeyword},
        {U"globals", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(context)},
        {U"locals", ParameterKind::PositionalOrKeyword, at(CoreObject::None).get(context)},
        {U"fromlist", ParameterKind::PositionalOrKeyword, at(CoreObject::EmptyTuple).get(context)},
        {U"level", ParameterKind::PositionalOrKeyword, at(CoreObject::IntZero).get(context)}};
    ReturnSlot callable(context), key(context);
    context.allocate_instance<NativeFunctionObject>(at(CoreObject::NativeFunctionType), callable,
                                                    "__import__", signature, builtin_import,
                                                    std::vector<Value>{});
    context.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__import__");
    name_store(context, mapping, key.value(), callable.value());
}
} // namespace cxxpython::core
