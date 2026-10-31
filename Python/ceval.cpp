#include "cxxpython/asyncgenobject.hpp"
#include "cxxpython/execution.hpp"
#include "cxxpython/genobject.hpp"
#include "cxxpython/import.hpp"
#include "cxxpython/setobject.hpp"
#include "cxxpython/typealiasobject.hpp"
#include "cxxpython/typevarobject.hpp"
#include "internal/aiterobject.hpp"
#include "internal/errors.hpp"
#include "internal/exceptiongroup.hpp"
#include "internal/patternmatching.hpp"
#include "internal/setobject.hpp"
#include "internal/sysmodule.hpp"

#include <algorithm>
#include <charconv>

namespace cxxpython::core {
namespace detail {
bool delegation_opcode(Opcode opcode)
{
    return opcode == Opcode::YieldFrom || opcode == Opcode::ForAwait;
}
class FrameActivation final {
  public:
    FrameActivation(Attachment &context, ReturnSlot &active, bool generator = false)
        : context_(context), active_(active), root_(active.value()), current_(root_),
          previous_(context.current_frame_), previous_exception_(context.handled_exception_),
          generator_(generator)
    {
        auto &data = context.payload<FrameObject>(root_);
        context.store(root_, data.back, previous_);
        inherited(data, previous_exception_);
        data.state.store(FrameState::Running, std::memory_order_relaxed);
        data.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
        activate(root_);
    }
    ~FrameActivation()
    {
        while (current() != root_) {
            ReturnSlot parent(context_);
            context_.load(current_, frame().back, parent);
            complete(frame());
            activate(parent.value());
            context_.leave_call();
        }
        if (suspended_)
            inherited(frame(), {});
        else
            complete(frame());
        if (generator_)
            context_.store(root_, frame().back, {});
        context_.current_frame_ = previous_;
        context_.handled_exception_ = previous_exception_;
    }
    Value current() const noexcept
    {
        return current_;
    }
    FrameObject &frame() const noexcept
    {
        return *frame_;
    }
    const CodeObject &code() const noexcept
    {
        return *code_;
    }
    void sync()
    {
        context_.handled_exception_ = frame().handled_exception;
    }
    void suspend() noexcept
    {
        suspended_ = true;
    }
    bool suspended() const noexcept
    {
        return suspended_;
    }
    bool descend(Handle frame)
    {
        if (!context_.enter_call())
            return false;
        auto &child = context_.payload<FrameObject>(frame);
        context_.store(frame.get(context_), child.back, current());
        child.handled_exception = context_.handled_exception_;
        child.state.store(FrameState::Running, std::memory_order_relaxed);
        child.owner.store(std::this_thread::get_id(), std::memory_order_relaxed);
        activate(frame.get(context_));
        return true;
    }
    bool returned(Value value, ReturnSlot &out)
    {
        if (current() == root_) {
            out.ok(value);
            return true;
        }
        auto &child = frame();
        ReturnSlot parent_root(context_);
        context_.load(current_, child.back, parent_root);
        const auto parent_value = parent_root.value();
        auto &parent = context_.payload<FrameObject>(parent_value);
        const auto base = *parent.pending_call;
        // Publish into the parent's traced stack before dropping the child root.
        parent.operands[base] = value;
        for (std::size_t i = base + 1; i < parent.sp; ++i)
            parent.operands[i] = context_.builtin(CoreObject::None);
        parent.sp = base + 1;
        parent.pending_call.reset();
        complete(child);
        activate(parent_value);
        context_.leave_call();
        return false;
    }
    void raising(Value exception, bool traceback = true)
    {
        auto &data = frame();
        const auto lasti = data.last_instruction.load(std::memory_order_relaxed);
        if (lasti != std::numeric_limits<std::size_t>::max() &&
            delegation_opcode(code().program.instructions()[lasti].opcode)) {
            // Also covers host allocation failures before delegate_step returns.
            context_.store(current_, data.delegate, {});
            data.resume_value.reset();
            data.resume_exception.reset();
            data.resume_throw_args.reset();
        }
        data.reason = ExceptionRaise{exception};
        data.pending_call.reset();
        if (traceback && exception != context_.builtin(CoreObject::MemoryError)) {
            RootScope roots(context_);
            ReturnSlot out(context_);
            append_traceback(context_, roots.root(exception), roots.root(current()), out);
            data.reason = ExceptionRaise{out.exception()};
        }
    }
    bool failed(Value exception, ReturnSlot &out)
    {
        if (current() == root_) {
            out.raised(exception);
            return true;
        }
        auto &child = frame();
        ReturnSlot parent_root(context_);
        context_.load(current_, child.back, parent_root);
        const auto parent_value = parent_root.value();
        auto &parent = context_.payload<FrameObject>(parent_value);
        parent.reason = ExceptionRaise{exception};
        parent.trim_stack(context_, *parent.pending_call);
        parent.pending_call.reset();
        complete(child);
        activate(parent_value);
        context_.leave_call();
        raising(exception);
        return false;
    }

  private:
    static void inherited(FrameObject &frame, std::optional<Value> value) noexcept
    {
        // Borrowed exception state follows the resuming caller, not the generator.
        if (frame.handled_inherited)
            frame.handled_exception = value;
        for (auto &region : frame.regions)
            if (region.previous_inherited)
                region.previous_exception = value;
        for (auto &cleanup : frame.cleanups)
            if (cleanup.previous_inherited)
                cleanup.previous_exception = value;
    }
    void activate(Value value)
    {
        // The active slot roots this nonmoving Frame and its immutable Code.
        // Refresh borrowed payloads only when the scheduler changes frames.
        active_.ok(value);
        current_ = value;
        frame_ = &context_.payload<FrameObject>(value);
        code_ = &context_.payload<CodeObject>(frame_->code);
        context_.current_frame_ = value;
        sync();
    }
    void complete(FrameObject &frame) noexcept
    {
        frame.trim_stack(context_, 0);
        frame.pending_call.reset();
        frame.regions.clear();
        frame.cleanups.clear();
        frame.reason.reset();
        frame.handled_exception.reset();
        frame.resume_value.reset();
        frame.resume_exception.reset();
        frame.resume_throw_args.reset();
        context_.store(current_, frame.delegate, {});
        if (!generator_ || current_ != root_) {
            frame.owner.store({}, std::memory_order_relaxed);
            frame.state.store(FrameState::Completed, std::memory_order_release);
        }
    }
    Attachment &context_;
    ReturnSlot &active_;
    Value root_;
    Value current_;
    FrameObject *frame_ = nullptr;
    const CodeObject *code_ = nullptr;
    std::optional<Value> previous_;
    std::optional<Value> previous_exception_;
    bool generator_ = false;
    bool suspended_ = false;
};
} // namespace detail
namespace {
void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
{
    raise_error(context, CoreObject::TypeErrorType, std::move(message), out);
}
bool raised(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}
template <class T, CoreObject Kind> bool kind(Attachment &context, Value value)
{
    return is_subtype(context, context.type_of(value), context.builtin(Kind)) &&
           context.has_layout<T>(value);
}
void missing_name(Attachment &context, const std::u32string &name, ReturnSlot &out,
                  std::optional<std::u32string> message = {})
{
    raise_error(context, CoreObject::NameErrorType,
                message.value_or(U"name '" + name + U"' is not defined"), out);
    if (out.tag() == ResultTag::Raised &&
        context.type_of(out.exception()) == context.builtin(CoreObject::NameErrorType)) {
        RootScope roots(context);
        auto error = roots.root(out.exception());
        ReturnSlot text(context);
        make_str(context, name, text);
        context.store(error.get(context), context.instance_slot(error.get(context), 0),
                      text.value());
    }
}
void unbound(Attachment &context, const std::u32string &name, ReturnSlot &out, bool free = false)
{
    if (free)
        missing_name(context, name, out,
                     U"cannot access free variable '" + name +
                         U"' where it is not associated with a value in enclosing scope");
    else
        raise_error(context, CoreObject::UnboundLocalErrorType,
                    U"local variable '" + name + U"' is not bound", out);
}
void with_method(Attachment &context, Handle manager, Protocol protocol, ReturnSlot &out)
{
    RootScope roots(context);
    auto slot = resolve_protocol(context, roots.root(context.type_of(manager)), protocol, roots);
    if (slot.state == SlotState::Missing) {
        type_error(context,
                   U"context manager is missing " +
                       std::u32string(protocol_definition(protocol).name),
                   out);
        return;
    }
    detail::bind_descriptor(context, slot.descriptor.get(context), manager.get(context),
                            slot.type.get(context), out);
}
void make_frame(Attachment &context, Handle function, CallArgsView args,
                std::optional<Value> receiver, ReturnSlot &out,
                std::optional<Handle> namespace_mapping = {})
{
    RootScope roots(context);
    const auto &callable = context.payload<FunctionObject>(function);
    auto code = roots.root(callable.code);
    ReturnSlot default_values(context), keyword_values(context);
    context.load(function.get(context), callable.defaults, default_values);
    auto defaults =
        roots.root(default_values.tag() == ResultTag::Ok ? default_values.value()
                                                         : context.builtin(CoreObject::EmptyTuple));
    std::optional<Value> keywords;
    context.load(function.get(context), callable.kwdefaults, keyword_values);
    if (keyword_values.tag() == ResultTag::Ok)
        keywords = keyword_values.value();
    const auto &program = context.payload<CodeObject>(code).program;
    std::optional<Value> mapping;
    ReturnSlot space(context);
    if (namespace_mapping)
        mapping = roots.root(namespace_mapping->get(context)).get(context);
    else if (program.kind() == CodeKind::Module)
        mapping = callable.globals;
    else if (program.kind() == CodeKind::ClassBody) {
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              space);
        mapping = space.value();
    }
    std::vector<Value> bound;
    detail::FunctionDefaults values{context.payload<TupleObject>(defaults).items, keywords};
    if (!detail::bind_arguments(context, program.signature(), args, receiver, roots, bound, out,
                                values))
        return;
    std::vector<Value> cells;
    cells.reserve(program.cell_names().size() + program.free_names().size());
    if (!program.cell_locals().empty()) {
        const auto type = roots.root(context.builtin(CoreObject::CellType));
        ReturnSlot cell(context);
        for (auto local : program.cell_locals()) {
            context.allocate_instance<CellObject>(type, cell,
                                                  local && *local < bound.size()
                                                      ? std::optional<Value>{bound[*local]}
                                                      : std::optional<Value>{});
            cells.push_back(roots.root(cell.value()).get(context));
        }
    }
    if (callable.closure)
        for (auto cell : context.payload<TupleObject>(*callable.closure).items)
            cells.push_back(cell);
    context.allocate_instance<FrameObject>(
        roots.root(context.builtin(CoreObject::FrameType)), out, function.get(context),
        code.get(context), program.local_names().size(), program.max_stack(),
        context.builtin(CoreObject::None), program.max_regions(), std::move(cells), mapping);
    auto &data = context.payload<FrameObject>(out.value());
    for (std::size_t i = 0; i < bound.size(); ++i)
        data.locals[i] = bound[i];
    for (auto local : program.cell_locals())
        if (local)
            data.locals[*local].reset();
}
void namespace_lookup(Attachment &context, Value mapping, Value key, ReturnSlot &out)
{
    if (context.type_of(mapping) == context.builtin(CoreObject::DictType)) {
        RootScope roots(context);
        dict_lookup(context, roots.root(mapping), roots.root(key), out);
        return;
    }
    RootScope roots(context);
    get_item(context, roots.root(mapping), roots.root(key), out);
    if (raised(out) && is_subtype(context, context.type_of(out.exception()),
                                  context.builtin(CoreObject::KeyErrorType)))
        out.reset();
}
void import_locals(Attachment &context, FrameObject &frame, const CodeObject &code, ReturnSlot &out)
{
    if (frame.namespace_mapping) {
        out.ok(*frame.namespace_mapping);
        return;
    }
    RootScope roots(context);
    if (!frame.locals_snapshot) {
        context.allocate_instance<DictObject>(roots.root(context.builtin(CoreObject::DictType)),
                                              out);
        frame.locals_snapshot = out.value();
    }
    const auto mapping = roots.root(*frame.locals_snapshot);
    ReturnSlot key(context), value(context);
    auto store = [&](const std::u32string &name, Value item) {
        const auto rooted = roots.root(item);
        make_str(context, name, key);
        dict_store(context, mapping, roots.root(key.value()), rooted, out);
    };
    auto erase = [&](const std::u32string &name) {
        make_str(context, name, key);
        dict_erase(context, mapping, roots.root(key.value()), out);
    };
    const auto cell_locals = code.program.cell_locals();
    for (std::size_t i = 0; i < frame.locals.size(); ++i) {
        auto item = frame.locals[i];
        const auto cell = std::find(cell_locals.begin(), cell_locals.end(), i);
        if (cell != cell_locals.end()) {
            read_cell(context, frame.cells[cell - cell_locals.begin()], value);
            item = value.tag() == ResultTag::Empty ? std::optional<Value>{} : value.value();
        }
        const auto &name = code.program.local_names()[i];
        if (item)
            store(name, *item);
        else
            erase(name);
        if (raised(out))
            return;
    }
    for (std::size_t i = cell_locals.size(); i < frame.cells.size(); ++i) {
        read_cell(context, frame.cells[i], value);
        const auto &name = code.program.free_names()[i - cell_locals.size()];
        if (value.tag() != ResultTag::Empty)
            store(name, value.value());
        else
            erase(name);
        if (raised(out))
            return;
    }
    out.ok(mapping.get(context));
}
void import_star(Attachment &context, Handle from, Handle mapping, ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot key(context), names(context), value(context), index(context);
    make_str(context, U"__all__", key);
    get_attribute(context, from, roots.root(key.value()), names);
    bool skip_private = false;
    if (raised(names)) {
        if (!is_subtype(context, context.type_of(names.exception()),
                        context.builtin(CoreObject::AttributeErrorType))) {
            out.raised(names.exception());
            return;
        }
        skip_private = true;
        make_str(context, U"__dict__", key);
        get_attribute(context, from, roots.root(key.value()), names);
        if (raised(names)) {
            if (is_subtype(context, context.type_of(names.exception()),
                           context.builtin(CoreObject::AttributeErrorType)))
                raise_error(context, CoreObject::ImportErrorType,
                            U"from-import-* object has no __dict__ and no __all__", out);
            else
                out.raised(names.exception());
            return;
        }
        if (context.type_of(names.value()) == context.builtin(CoreObject::DictType)) {
            DictView view(context, names.value());
            std::vector<Value> keys;
            for (const auto &entry : view.entries())
                keys.push_back(entry.key);
            cxxpython::core::make_tuple(context, keys, names);
        } else {
            const auto dictionary = roots.root(names.value());
            make_str(context, U"keys", key);
            get_attribute(context, dictionary, roots.root(key.value()), names);
            if (!raised(names))
                call(context, roots.root(names.value()), {}, names);
            if (raised(names)) {
                out.raised(names.exception());
                return;
            }
            ReturnSlot iterator(context), item(context);
            get_iter(context, roots.root(names.value()), iterator);
            if (raised(iterator)) {
                out.raised(iterator.exception());
                return;
            }
            std::vector<Value> keys;
            for (;;) {
                const auto status = iter_next(context, roots.root(iterator.value()), item);
                if (status == NextStatus::Done)
                    break;
                if (status == NextStatus::Raised) {
                    out.raised(item.exception());
                    return;
                }
                keys.push_back(roots.root(item.value()).get(context));
            }
            cxxpython::core::make_tuple(context, keys, names);
        }
    }
    const auto all = roots.root(names.value());
    for (std::int64_t i = 0;; ++i) {
        RootScope iteration(context);
        make_int(context, i, index);
        get_item(context, all, iteration.root(index.value()), key);
        if (raised(key)) {
            if (is_subtype(context, context.type_of(key.exception()),
                           context.builtin(CoreObject::IndexErrorType)))
                out.ok(context.builtin(CoreObject::None));
            else
                out.raised(key.exception());
            return;
        }
        const auto name = iteration.root(key.value());
        if (!is_subtype(context, context.type_of(name), context.builtin(CoreObject::StrType))) {
            type_error(context, U"import * names must be str", out);
            return;
        }
        const auto &text = context.payload<StrObject>(name).value;
        if (!skip_private || text.empty() || text[0] != U'_') {
            get_attribute(context, from, name, value);
            if (raised(value)) {
                out.raised(value.exception());
                return;
            }
            if (context.type_of(mapping) == context.builtin(CoreObject::DictType))
                dict_store(context, mapping, name, iteration.root(value.value()), out);
            else
                set_item(context, mapping, name, iteration.root(value.value()), out);
            if (raised(out))
                return;
        }
        if (i == std::numeric_limits<std::int64_t>::max()) {
            raise_error(context, CoreObject::OverflowErrorType, U"too many import * names", out);
            return;
        }
    }
}
enum class DelegateResult { Yielded, Returned, Raised, Rejected };
DelegateResult delegate_step(Attachment &context, Value frame_value, FrameObject &frame,
                             ReturnSlot &out)
{
    RootScope roots(context);
    ReturnSlot target(context);
    context.load(frame_value, frame.delegate, target);
    const auto iterator = roots.root(target.value());
    const auto sent = roots.root(frame.resume_value.value_or(context.builtin(CoreObject::None)));
    std::optional<Handle> thrown, arguments;
    if (frame.resume_exception)
        thrown = roots.root(*frame.resume_exception);
    if (frame.resume_throw_args)
        arguments = roots.root(*frame.resume_throw_args);
    frame.resume_value.reset();
    frame.resume_exception.reset();
    frame.resume_throw_args.reset();
    auto method = [&](const char32_t *name) {
        make_str(context, name, out);
        if (raised(out)) {
            target.raised(out.exception());
            return;
        }
        get_attribute(context, iterator, roots.root(out.value()), target);
    };
    auto missing = [&] {
        return target.tag() == ResultTag::Raised &&
               is_subtype(context, context.type_of(target.exception()),
                          context.builtin(CoreObject::AttributeErrorType));
    };
    auto normalize = [&] {
        if (arguments)
            detail::normalize_generator_throw(context,
                                              context.payload<TupleObject>(*arguments).items, out);
        else
            out.ok(thrown->get(context));
        if (raised(out))
            return DelegateResult::Rejected;
        out.raised(out.value());
        return DelegateResult::Raised;
    };
    if (thrown || arguments) {
        const auto type =
            arguments ? context.payload<TupleObject>(*arguments).items[0] : thrown->get(context);
        const auto exit_type = context.builtin(CoreObject::GeneratorExitType);
        const bool closing = is_subtype(context, context.type_of(type), exit_type) ||
                             (context.has_layout<TypeObject>(type) &&
                              is_subtype(context, context.type_of(type), context.type_type()) &&
                              is_subtype(context, type, exit_type));
        if (closing) {
            method(U"close");
            if (!missing()) {
                if (target.tag() == ResultTag::Raised) {
                    detail::write_unraisable(context, roots.root(target.exception()), iterator);
                } else {
                    call(context, roots.root(target.value()), {}, out);
                    if (raised(out))
                        return DelegateResult::Raised;
                }
            }
            return normalize();
        }
        method(U"throw");
        if (missing()) {
            return normalize();
        }
        if (target.tag() == ResultTag::Raised) {
            out.raised(target.exception());
            return DelegateResult::Rejected;
        }
        const Value normalized[] = {thrown ? thrown->get(context)
                                           : context.builtin(CoreObject::None)};
        call(context, roots.root(target.value()),
             {arguments ? std::span<const Value>{context.payload<TupleObject>(*arguments).items}
                        : std::span<const Value>{normalized}},
             out);
    } else if (sent.get(context) == context.builtin(CoreObject::None))
        iter_next(context, iterator, out);
    else {
        method(U"send");
        if (target.tag() == ResultTag::Raised) {
            out.raised(target.exception());
            return DelegateResult::Raised;
        }
        const Value values[] = {sent.get(context)};
        call(context, roots.root(target.value()), {values}, out);
    }
    if (!raised(out))
        return DelegateResult::Yielded;
    if (!is_subtype(context, context.type_of(out.exception()),
                    context.builtin(CoreObject::StopIterationType)))
        return DelegateResult::Raised;
    const auto stopped = roots.root(out.exception());
    context.load(stopped.get(context), context.instance_slot(stopped.get(context), 0), out);
    if (out.tag() == ResultTag::Empty)
        out.ok(context.builtin(CoreObject::None));
    return DelegateResult::Returned;
}
void run(Attachment &context, detail::FrameActivation &activation, ReturnSlot &out)
{
    ReturnSlot result(context);
    std::size_t poll_budget = 64;
    for (;;) {
        if (!--poll_budget) {
            context.poll();
            poll_budget = 64;
        }
        auto &data = activation.frame();
        const auto &code = activation.code();
        try {
            if (data.reason) {
                context.poll();
                if (data.unwind(context)) {
                    activation.sync();
                    continue;
                }
                if (const auto *returned = std::get_if<FunctionReturn>(&*data.reason)) {
                    if (activation.returned(returned->value, out))
                        return;
                } else if (const auto *error = std::get_if<ExceptionRaise>(&*data.reason)) {
                    if (activation.failed(error->exception, out))
                        return;
                } else
                    throw std::logic_error("unverified control reason escaped its Frame");
                continue;
            }
            data.last_instruction.store(data.pc, std::memory_order_release);
            const auto [opcode, operand] = code.program.instructions()[data.pc++];
            switch (opcode) {
            case Opcode::LoadConst:
                data.operands[data.sp++] =
                    context.payload<TupleObject>(code.constants).items[operand];
                break;
            case Opcode::LoadFast:
                if (!data.locals[operand]) {
                    unbound(context, code.program.local_names()[operand], result);
                    activation.raising(result.exception());
                    break;
                }
                data.operands[data.sp++] = *data.locals[operand];
                break;
            case Opcode::LoadClosure:
                data.operands[data.sp++] = data.cells[operand];
                break;
            case Opcode::LoadClassDeref: {
                const auto offset = operand - code.program.cell_names().size();
                const auto key = context.payload<TupleObject>(code.freevars).items[offset];
                namespace_lookup(context, *data.namespace_mapping, key, result);
                if (result.tag() == ResultTag::Empty)
                    read_cell(context, data.cells[operand], result);
                if (result.tag() == ResultTag::Empty)
                    unbound(context, code.program.free_names()[offset], result, true);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::LoadDeref:
            case Opcode::DeleteDeref: {
                if (opcode == Opcode::LoadDeref)
                    read_cell(context, data.cells[operand], result);
                else
                    context.exchange(data.cells[operand],
                                     context.payload<CellObject>(data.cells[operand]).contents, {},
                                     result);
                if (result.tag() == ResultTag::Empty) {
                    const auto local_cells = code.program.cell_names().size();
                    const bool free = operand >= local_cells;
                    unbound(context,
                            free ? code.program.free_names()[operand - local_cells]
                                 : code.program.cell_names()[operand],
                            result, free);
                    activation.raising(result.exception());
                } else if (opcode == Opcode::LoadDeref)
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::LoadFromDictOrGlobals:
            case Opcode::LoadFromDictOrDeref: {
                RootScope roots(context);
                auto mapping = roots.root(data.operands[data.sp - 1]);
                Value key = context.builtin(CoreObject::None);
                if (opcode == Opcode::LoadFromDictOrGlobals)
                    key = context.payload<TupleObject>(code.names).items[operand];
                else if (operand < code.program.cell_names().size())
                    key = context.payload<TupleObject>(code.cellvars).items[operand];
                else
                    key = context.payload<TupleObject>(code.freevars)
                              .items[operand - code.program.cell_names().size()];
                auto name = roots.root(key);
                namespace_lookup(context, mapping.get(context), key, result);
                if (result.tag() == ResultTag::Empty) {
                    if (opcode == Opcode::LoadFromDictOrDeref) {
                        read_cell(context, data.cells[operand], result);
                        if (result.tag() == ResultTag::Empty)
                            unbound(context, context.payload<StrObject>(name).value, result,
                                    operand >= code.program.cell_names().size());
                    } else {
                        const auto &function = context.payload<FunctionObject>(data.function);
                        namespace_lookup(context, function.globals, key, result);
                        if (result.tag() == ResultTag::Empty)
                            namespace_lookup(context, function.builtins, key, result);
                        if (result.tag() == ResultTag::Empty)
                            missing_name(context, context.payload<StrObject>(name).value, result);
                    }
                }
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::MakeTypeVar: {
                RootScope roots(context);
                auto name = roots.root(data.operands[data.sp - 3]);
                auto bound = roots.root(data.operands[data.sp - 2]);
                auto constraints = roots.root(data.operands[data.sp - 1]);
                detail::make_typevar(context, name,
                                     bound.get(context) == context.builtin(CoreObject::None)
                                         ? std::nullopt
                                         : std::optional{bound},
                                     constraints.get(context) == context.builtin(CoreObject::None)
                                         ? std::nullopt
                                         : std::optional{constraints},
                                     result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.operands[data.sp - 3] = result.value();
                    data.trim_stack(context, data.sp - 2);
                }
                break;
            }
            case Opcode::MakeParamSpec:
            case Opcode::MakeTypeVarTuple:
            case Opcode::MakeGenericBase: {
                RootScope roots(context);
                auto value = roots.root(data.operands[data.sp - 1]);
                if (opcode == Opcode::MakeParamSpec)
                    detail::make_paramspec(context, value, result);
                else if (opcode == Opcode::MakeTypeVarTuple)
                    detail::make_typevartuple(context, value, result);
                else if (!kind<TupleObject, CoreObject::TupleType>(context, value.get(context)))
                    raise_error(context, CoreObject::TypeErrorType,
                                U"generic parameters must be a tuple", result);
                else
                    detail::make_generic_base(context, value, result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::SetFunctionTypeParams: {
                const auto function = data.operands[data.sp - 2],
                           parameters = data.operands[data.sp - 1];
                if (context.type_of(function) != context.builtin(CoreObject::FunctionType) ||
                    !context.has_layout<FunctionObject>(function) ||
                    !kind<TupleObject, CoreObject::TupleType>(context, parameters)) {
                    raise_error(context, CoreObject::TypeErrorType,
                                U"invalid function type parameters", result);
                    activation.raising(result.exception());
                } else {
                    context.store(function, context.payload<FunctionObject>(function).type_params,
                                  parameters);
                    data.trim_stack(context, data.sp - 1);
                }
                break;
            }
            case Opcode::MakeTypeAlias: {
                RootScope roots(context);
                detail::make_type_alias(context, roots.root(data.operands[data.sp - 3]),
                                        roots.root(data.operands[data.sp - 2]),
                                        roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.operands[data.sp - 3] = result.value();
                    data.trim_stack(context, data.sp - 2);
                }
                break;
            }
            case Opcode::StoreDeref:
                write_cell(context, data.cells[operand], data.operands[data.sp - 1]);
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            case Opcode::ImportName: {
                RootScope roots(context);
                const auto &function = context.payload<FunctionObject>(data.function);
                make_str(context, U"__import__", result);
                namespace_lookup(context, function.builtins,
                                 roots.root(result.value()).get(context), result);
                if (result.tag() == ResultTag::Empty)
                    raise_error(context, CoreObject::ImportErrorType, U"__import__ not found",
                                result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                const auto callable = roots.root(result.value());
                import_locals(context, data, code, result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                const std::array arguments{context.payload<TupleObject>(code.names).items[operand],
                                           function.globals, result.value(),
                                           data.operands[data.sp - 1], data.operands[data.sp - 2]};
                call(context, callable, {arguments}, result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.trim_stack(context, data.sp - 2);
                    data.operands[data.sp++] = result.value();
                }
                break;
            }
            case Opcode::ImportFrom: {
                RootScope roots(context);
                import_from(context, roots.root(data.operands[data.sp - 1]),
                            roots.root(context.payload<TupleObject>(code.names).items[operand]),
                            result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::ImportStar: {
                RootScope roots(context);
                import_star(context, roots.root(data.operands[data.sp - 1]),
                            roots.root(*data.namespace_mapping), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.trim_stack(context, data.sp - 1);
                break;
            }
            case Opcode::LoadBuildClass: {
                RootScope roots(context);
                const auto &function = context.payload<FunctionObject>(data.function);
                make_str(context, U"__build_class__", result);
                namespace_lookup(context, function.builtins,
                                 roots.root(result.value()).get(context), result);
                if (result.tag() == ResultTag::Empty)
                    missing_name(context, U"__build_class__", result, U"__build_class__ not found");
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::LoadName:
            case Opcode::StoreName:
            case Opcode::DeleteName: {
                const auto mapping = *data.namespace_mapping;
                const auto key = context.payload<TupleObject>(code.names).items[operand];
                const auto &name = context.payload<StrObject>(key).value;
                if (opcode == Opcode::LoadName) {
                    namespace_lookup(context, mapping, key, result);
                    if (result.tag() == ResultTag::Empty) {
                        const auto &function = context.payload<FunctionObject>(data.function);
                        namespace_lookup(context, function.globals, key, result);
                        if (result.tag() == ResultTag::Empty)
                            namespace_lookup(context, function.builtins, key, result);
                        if (result.tag() == ResultTag::Empty)
                            missing_name(context, name, result);
                    }
                    if (!raised(result))
                        data.operands[data.sp++] = result.value();
                } else if (context.type_of(mapping) == context.builtin(CoreObject::DictType)) {
                    RootScope roots(context);
                    result.reset();
                    if (opcode == Opcode::StoreName)
                        dict_store(context, roots.root(mapping), roots.root(key),
                                   roots.root(data.operands[data.sp - 1]), result);
                    else {
                        dict_erase(context, roots.root(mapping), roots.root(key), result);
                        if (result.tag() == ResultTag::Empty)
                            missing_name(context, name, result);
                    }
                } else {
                    RootScope roots(context);
                    if (opcode == Opcode::StoreName)
                        set_item(context, roots.root(mapping), roots.root(key),
                                 roots.root(data.operands[data.sp - 1]), result);
                    else {
                        del_item(context, roots.root(mapping), roots.root(key), result);
                        if (raised(result) &&
                            is_subtype(context, context.type_of(result.exception()),
                                       context.builtin(CoreObject::KeyErrorType)))
                            missing_name(context, name, result);
                    }
                }
                if (raised(result))
                    activation.raising(result.exception());
                else if (opcode == Opcode::StoreName)
                    data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            }
            case Opcode::SetupAnnotations: {
                RootScope roots(context);
                auto mapping = roots.root(*data.namespace_mapping);
                auto key = roots.root(context.payload<TupleObject>(code.names).items[operand]);
                namespace_lookup(context, mapping.get(context), key.get(context), result);
                if (result.tag() == ResultTag::Empty) {
                    ReturnSlot dictionary(context);
                    context.allocate_instance<DictObject>(
                        roots.root(context.builtin(CoreObject::DictType)), dictionary);
                    auto annotations = roots.root(dictionary.value());
                    if (context.type_of(mapping) == context.builtin(CoreObject::DictType))
                        dict_setdefault(context, mapping, key, annotations, result);
                    else
                        set_item(context, mapping, key, annotations, result);
                }
                if (raised(result))
                    activation.raising(result.exception());
                break;
            }
            case Opcode::LoadGlobal:
            case Opcode::StoreGlobal:
            case Opcode::DeleteGlobal: {
                const auto &function = context.payload<FunctionObject>(data.function);
                const auto key = context.payload<TupleObject>(code.names).items[operand];
                const auto &name = context.payload<StrObject>(key).value;
                if (opcode == Opcode::LoadGlobal) {
                    namespace_lookup(context, function.globals, key, result);
                    if (result.tag() == ResultTag::Empty)
                        namespace_lookup(context, function.builtins, key, result);
                    if (result.tag() == ResultTag::Empty)
                        missing_name(context, name, result);
                    if (raised(result))
                        activation.raising(result.exception());
                    else
                        data.operands[data.sp++] = result.value();
                } else if (opcode == Opcode::StoreGlobal) {
                    RootScope roots(context);
                    dict_store(context, roots.root(function.globals), roots.root(key),
                               roots.root(data.operands[data.sp - 1]), result);
                    if (raised(result))
                        activation.raising(result.exception());
                    else
                        data.operands[--data.sp] = context.builtin(CoreObject::None);
                } else {
                    RootScope roots(context);
                    dict_erase(context, roots.root(function.globals), roots.root(key), result);
                    if (result.tag() == ResultTag::Empty)
                        missing_name(context, name, result);
                    if (raised(result))
                        activation.raising(result.exception());
                }
                break;
            }
            case Opcode::StoreFast:
                data.locals[operand] = data.operands[--data.sp];
                data.operands[data.sp] = context.builtin(CoreObject::None);
                break;
            case Opcode::MakeFunction: {
                RootScope roots(context);
                const auto &parent = context.payload<FunctionObject>(data.function);
                auto cursor = data.sp;
                const auto child_code = roots.root(data.operands[--cursor]);
                FunctionSpec spec{child_code, roots.root(parent.globals)};
                spec.builtins = roots.root(parent.builtins);
                if (operand & static_cast<std::uint32_t>(FunctionFlags::Closure))
                    spec.closure = roots.root(data.operands[--cursor]);
                if (operand & static_cast<std::uint32_t>(FunctionFlags::Annotations))
                    spec.annotations = roots.root(data.operands[--cursor]);
                if (operand & static_cast<std::uint32_t>(FunctionFlags::KeywordDefaults))
                    spec.kwdefaults = roots.root(data.operands[--cursor]);
                if (operand & static_cast<std::uint32_t>(FunctionFlags::Defaults))
                    spec.defaults = roots.root(data.operands[--cursor]);
                make_function(context, spec, result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.trim_stack(context, cursor);
                data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::DeleteFast:
                if (!data.locals[operand]) {
                    unbound(context, code.program.local_names()[operand], result);
                    activation.raising(result.exception());
                    break;
                }
                data.locals[operand].reset();
                break;
            case Opcode::PopTop:
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            case Opcode::DupTop:
                data.operands[data.sp] = data.operands[data.sp - 1];
                ++data.sp;
                break;
            case Opcode::Swap:
                std::swap(data.operands[data.sp - 1], data.operands[data.sp - operand]);
                break;
            case Opcode::Copy:
                data.operands[data.sp] = data.operands[data.sp - operand];
                ++data.sp;
                break;
            case Opcode::GetIter: {
                RootScope roots(context);
                get_iter(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::GetAiter: {
                RootScope roots(context);
                detail::get_async_iter(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::ForIter: {
                RootScope roots(context);
                const auto status =
                    iter_next(context, roots.root(data.operands[data.sp - 1]), result);
                if (status == NextStatus::Raised)
                    activation.raising(result.exception());
                else if (status == NextStatus::Done) {
                    data.pc = operand;
                } else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::UnpackSequence:
            case Opcode::UnpackEx: {
                RootScope roots(context);
                ReturnSlot iterator(context);
                get_iter(context, roots.root(data.operands[data.sp - 1]), iterator);
                if (raised(iterator)) {
                    // Match ceval.c's non-iterable unpack diagnostic without replaying __iter__.
                    if (is_subtype(context, context.type_of(iterator.exception()),
                                   context.builtin(CoreObject::TypeErrorType))) {
                        const auto type = roots.root(context.type_of(data.operands[data.sp - 1]));
                        const auto iter = resolve_protocol(context, type, Protocol::Iter, roots);
                        const auto item = resolve_protocol(context, type, Protocol::GetItem, roots);
                        if (iter.state == SlotState::Missing && item.state == SlotState::Missing) {
                            const auto metadata = TypeView(context, type);
                            const auto name = metadata.name();
                            const auto text =
                                std::holds_alternative<Value>(name)
                                    ? context.payload<StrObject>(std::get<Value>(name)).value
                                    : std::get<std::u32string>(name);
                            raise_error(context, CoreObject::TypeErrorType,
                                        U"cannot unpack non-iterable " + text + U" object",
                                        iterator);
                        }
                    }
                    activation.raising(iterator.exception());
                    break;
                }
                const auto source = roots.root(iterator.value());
                std::vector<Value> values;
                const auto before = opcode == Opcode::UnpackEx ? operand & 65535U : operand;
                const auto after = opcode == Opcode::UnpackEx ? operand >> 16 : 0;
                values.reserve(static_cast<std::size_t>(before) + after);
                bool failed = false;
                for (std::size_t i = 0; i < before; ++i) {
                    const auto status = iter_next(context, source, result);
                    if (status != NextStatus::Item) {
                        if (status == NextStatus::Done)
                            raise_error(context, CoreObject::ValueErrorType,
                                        U"not enough values to unpack", result);
                        activation.raising(result.exception());
                        failed = true;
                        break;
                    }
                    values.push_back(roots.root(result.value()).get(context));
                }
                if (failed)
                    break;
                if (opcode == Opcode::UnpackEx) {
                    for (;;) {
                        const auto status = iter_next(context, source, result);
                        if (status == NextStatus::Done)
                            break;
                        if (status == NextStatus::Raised) {
                            activation.raising(result.exception());
                            failed = true;
                            break;
                        }
                        values.push_back(roots.root(result.value()).get(context));
                    }
                    if (failed)
                        break;
                    if (values.size() - before < after) {
                        raise_error(context, CoreObject::ValueErrorType,
                                    U"not enough values to unpack", result);
                        activation.raising(result.exception());
                        break;
                    }
                    const auto rest = values.size() - before - after;
                    make_list(context, std::span<const Value>{values.data() + before, rest},
                              result);
                    if (raised(result)) {
                        activation.raising(result.exception());
                        break;
                    }
                    const auto middle = roots.root(result.value());
                    std::vector<Value> unpacked;
                    unpacked.reserve(static_cast<std::size_t>(before) + after + 1);
                    unpacked.insert(unpacked.end(), values.begin(), values.begin() + before);
                    unpacked.push_back(middle.get(context));
                    unpacked.insert(unpacked.end(), values.end() - after, values.end());
                    values.swap(unpacked);
                } else {
                    const auto status = iter_next(context, source, result);
                    if (status != NextStatus::Done) {
                        if (status == NextStatus::Item)
                            raise_error(context, CoreObject::ValueErrorType,
                                        U"too many values to unpack", result);
                        activation.raising(result.exception());
                        break;
                    }
                }
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                for (auto i = values.size(); i-- > 0;)
                    data.operands[data.sp++] = values[i];
                break;
            }
            case Opcode::PrintExpr: {
                RootScope roots(context);
                detail::display_value(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.trim_stack(context, data.sp - 1);
                break;
            }
            case Opcode::BuildMap: {
                const auto base = data.sp - static_cast<std::size_t>(operand) * 2;
                RootScope roots(context);
                ReturnSlot mapping(context);
                context.allocate_instance<DictObject>(
                    roots.root(context.builtin(CoreObject::DictType)), mapping);
                const auto owner = roots.root(mapping.value());
                result.reset(context);
                for (std::size_t i = base; i < data.sp; i += 2) {
                    dict_store(context, owner, roots.root(data.operands[i]),
                               roots.root(data.operands[i + 1]), result);
                    if (raised(result))
                        break;
                }
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.trim_stack(context, base);
                    data.operands[data.sp++] = owner.get(context);
                }
                break;
            }
            case Opcode::DictUpdate:
            case Opcode::DictMerge:
            case Opcode::ListAppend:
            case Opcode::ListExtend:
            case Opcode::SetAdd:
            case Opcode::SetUpdate: {
                RootScope roots(context);
                const auto owner = roots.root(data.operands[data.sp - 2]),
                           item = roots.root(data.operands[data.sp - 1]);
                if (opcode == Opcode::DictUpdate || opcode == Opcode::DictMerge) {
                    if (context.type_of(owner) != context.builtin(CoreObject::DictType))
                        type_error(context, U"dictionary builder requires an exact dict", result);
                    else
                        detail::dict_merge(context, owner, item, opcode == Opcode::DictMerge,
                                           result);
                } else if (opcode == Opcode::SetAdd || opcode == Opcode::SetUpdate) {
                    if (context.type_of(owner) != context.builtin(CoreObject::SetType))
                        type_error(context, U"set builder requires an exact set", result);
                    else if (opcode == Opcode::SetAdd)
                        set_add(context, owner, item, result);
                    else
                        detail::set_update(context, owner, item, result);
                } else if (context.type_of(owner) != context.builtin(CoreObject::ListType))
                    type_error(context, U"list builder requires an exact list", result);
                else if (opcode == Opcode::ListAppend)
                    detail::list_append(context, owner, item, result);
                else
                    detail::list_extend(context, owner, item, result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.trim_stack(context, data.sp - 1);
                break;
            }
            case Opcode::ListToTuple: {
                RootScope roots(context);
                const auto owner = roots.root(data.operands[data.sp - 1]);
                if (context.type_of(owner) != context.builtin(CoreObject::ListType))
                    type_error(context, U"list conversion requires an exact list", result);
                else {
                    std::vector<Value> items;
                    detail::list_snapshot(context, owner, roots, items);
                    cxxpython::core::make_tuple(context, items, result);
                }
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::LoadAssertionError:
                data.operands[data.sp++] = context.builtin(CoreObject::AssertionErrorType);
                break;
            case Opcode::BuildSlice: {
                RootScope roots(context);
                const auto base = data.sp - 3;
                make_slice(context, roots.root(data.operands[base]),
                           roots.root(data.operands[base + 1]), roots.root(data.operands[base + 2]),
                           result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.trim_stack(context, base);
                    data.operands[data.sp++] = result.value();
                }
                break;
            }
            case Opcode::FormatValue: {
                RootScope roots(context);
                const auto base = data.sp - 2;
                auto value = roots.root(data.operands[base]);
                const auto spec = roots.root(data.operands[base + 1]);
                if (operand) {
                    if (operand == 1)
                        str(context, value, result);
                    else if (operand == 2)
                        repr(context, value, result);
                    else
                        ascii(context, value, result);
                    if (raised(result)) {
                        activation.raising(result.exception());
                        break;
                    }
                    value = roots.root(result.value());
                }
                format(context, value, spec, result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.trim_stack(context, base);
                    data.operands[data.sp++] = result.value();
                }
                break;
            }
            case Opcode::BuildString: {
                const auto base = data.sp - operand;
                result.reset(context);
                std::u32string text;
                for (std::size_t i = base; i < data.sp; ++i) {
                    if (!kind<StrObject, CoreObject::StrType>(context, data.operands[i])) {
                        type_error(context, U"string builder requires string operands", result);
                        break;
                    }
                    const auto &part = context.payload<StrObject>(data.operands[i]).value;
                    if (part.size() > text.max_size() - text.size())
                        throw std::bad_alloc();
                    text += part;
                }
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    make_str(context, std::move(text), result);
                    if (raised(result))
                        activation.raising(result.exception());
                    else {
                        data.trim_stack(context, base);
                        data.operands[data.sp++] = result.value();
                    }
                }
                break;
            }
            case Opcode::StoreItem:
            case Opcode::DeleteItem: {
                const auto base = data.sp - (opcode == Opcode::StoreItem ? 3 : 2);
                RootScope roots(context);
                const auto owner = roots.root(data.operands[base]),
                           key = roots.root(data.operands[base + 1]);
                if (opcode == Opcode::StoreItem)
                    set_item(context, owner, key, roots.root(data.operands[base + 2]), result);
                else
                    del_item(context, owner, key, result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.trim_stack(context, base);
                break;
            }
            case Opcode::BuildTuple:
            case Opcode::BuildList:
            case Opcode::BuildSet:
            case Opcode::BuildNameDict: {
                const auto count = opcode == Opcode::BuildNameDict
                                       ? code.program.calls()[operand].keywords
                                       : operand;
                const auto base = data.sp - count;
                if (opcode == Opcode::BuildTuple)
                    cxxpython::core::make_tuple(
                        context, std::span<const Value>{data.operands.data() + base, count},
                        result);
                else if (opcode == Opcode::BuildList)
                    cxxpython::core::make_list(
                        context, std::span<const Value>{data.operands.data() + base, count},
                        result);
                else if (opcode == Opcode::BuildSet)
                    make_set(context, std::span<const Value>{data.operands.data() + base, count},
                             result);
                else {
                    RootScope roots(context);
                    context.allocate_instance<DictObject>(
                        roots.root(context.builtin(CoreObject::DictType)), result);
                    const auto &table = context.payload<TupleObject>(code.call_keywords).items;
                    const auto &names = context.payload<TupleObject>(table[operand]).items;
                    for (std::size_t i = 0; i < names.size(); ++i)
                        detail::name_store(context, result.value(), names[i],
                                           data.operands[base + i]);
                }
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.trim_stack(context, base);
                    data.operands[data.sp++] = result.value();
                }
                break;
            }
            case Opcode::Unary: {
                RootScope roots(context);
                unary(context, roots.root(data.operands[data.sp - 1]),
                      static_cast<UnaryOp>(operand), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::LoadAttr:
            case Opcode::StoreAttr:
            case Opcode::DeleteAttr: {
                RootScope roots(context);
                const auto name =
                    roots.root(context.payload<TupleObject>(code.names).items[operand]);
                const auto base = data.sp - (opcode == Opcode::StoreAttr ? 2 : 1);
                const auto object = roots.root(data.operands[base]);
                if (opcode == Opcode::LoadAttr)
                    get_attribute(context, object, name, result);
                else if (opcode == Opcode::StoreAttr)
                    set_attribute(context, object, name, roots.root(data.operands[base + 1]),
                                  result);
                else
                    del_attribute(context, object, name, result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.trim_stack(context, base);
                if (opcode == Opcode::LoadAttr)
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::Binary:
            case Opcode::Inplace:
            case Opcode::LoadItem:
            case Opcode::Compare: {
                RootScope roots(context);
                auto left = roots.root(data.operands[data.sp - 2]);
                auto right = roots.root(data.operands[data.sp - 1]);
                if (opcode == Opcode::Binary)
                    binary(context, left, right, static_cast<BinaryOp>(operand), result);
                else if (opcode == Opcode::Inplace)
                    inplace(context, left, right, static_cast<BinaryOp>(operand), result);
                else if (opcode == Opcode::LoadItem)
                    get_item(context, left, right, result);
                else
                    rich_compare(context, left, right, static_cast<CompareOp>(operand), result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.operands[data.sp - 2] = result.value();
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            }
            case Opcode::Jump:
                data.pc = operand;
                break;
            case Opcode::JumpIfFalse: {
                RootScope roots(context);
                truth(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                if (result.value() == context.builtin(CoreObject::False))
                    data.pc = operand;
                break;
            }
            case Opcode::Return:
                result.ok(data.operands[data.sp - 1]);
                detail::validate_return(context, result);
                data.reason = FunctionReturn{result.value()};
                break;
            case Opcode::Raise: {
                RootScope roots(context);
                const auto base = data.sp - operand;
                detail::normalize_exception(context, roots.root(data.operands[base]), result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                const auto exception = roots.root(result.value());
                if (operand == 2) {
                    std::optional<Value> cause;
                    if (data.operands[base + 1] != context.builtin(CoreObject::None)) {
                        ReturnSlot converted(context);
                        detail::normalize_exception(context, roots.root(data.operands[base + 1]),
                                                    converted);
                        if (raised(converted)) {
                            activation.raising(converted.exception());
                            break;
                        }
                        cause = roots.root(converted.value()).get(context);
                    }
                    auto &error = context.payload<ExceptionObject>(exception);
                    context.store(exception.get(context), error.cause, cause);
                    error.suppress_context.store(true, std::memory_order_release);
                }
                detail::link_exception_context(context, exception.get(context));
                activation.raising(exception.get(context));
                break;
            }
            case Opcode::RaiseInstance: {
                auto exception = data.operands[data.sp - 1];
                if (!kind<ExceptionObject, CoreObject::BaseExceptionType>(context, exception)) {
                    type_error(context, U"RAISE_INSTANCE requires a BaseException instance",
                               result);
                    exception = result.exception();
                } else
                    detail::link_exception_context(context, exception);
                activation.raising(exception);
                break;
            }
            case Opcode::Call:
            case Opcode::CallEx: {
                RootScope roots(context);
                const auto layout =
                    opcode == Opcode::Call ? code.program.calls()[operand] : CallLayout{0, 0};
                const auto base =
                    opcode == Opcode::Call ? data.sp - layout.argument_count() - 1 : data.sp - 3;
                std::vector<KeywordArg> named;
                std::span<const Value> positional;
                if (opcode == Opcode::CallEx) {
                    result.reset(context);
                    const auto tuple = roots.root(data.operands[base + 1]);
                    const auto dict = roots.root(data.operands[base + 2]);
                    if (context.type_of(tuple) != context.builtin(CoreObject::TupleType) ||
                        context.type_of(dict) != context.builtin(CoreObject::DictType)) {
                        type_error(context, U"expanded call requires tuple and dict arguments",
                                   result);
                        activation.raising(result.exception());
                        break;
                    }
                    positional = context.payload<TupleObject>(tuple).items;
                    DictView view(context, dict.get(context));
                    for (const auto &entry : view.entries()) {
                        if (!kind<StrObject, CoreObject::StrType>(context, entry.key)) {
                            type_error(context, U"keywords must be strings", result);
                            break;
                        }
                        named.push_back({roots.root(entry.key).get(context),
                                         roots.root(entry.value).get(context)});
                    }
                    if (raised(result)) {
                        activation.raising(result.exception());
                        break;
                    }
                } else {
                    const auto &keywords =
                        context
                            .payload<TupleObject>(
                                context.payload<TupleObject>(code.call_keywords).items[operand])
                            .items;
                    for (std::size_t i = 0; i < keywords.size(); ++i)
                        named.push_back(
                            {keywords[i], data.operands[base + 1 + layout.positional + i]});
                    positional = {data.operands.data() + base + 1, layout.positional};
                }
                CallArgsView args{positional, named};
                data.pending_call = base;
                auto prepared = detail::prepare_call(context, roots.root(data.operands[base]), args,
                                                     roots, result);
                if (prepared) {
                    ReturnSlot child(context);
                    make_frame(
                        context, prepared->function, {prepared->positional, prepared->keywords},
                        prepared->receiver ? std::optional<Value>{prepared->receiver->get(context)}
                                           : std::optional<Value>{},
                        child);
                    if (raised(child)) {
                        activation.raising(child.exception());
                        break;
                    }
                    const auto frame = roots.root(child.value());
                    if (context.payload<CodeObject>(context.payload<FrameObject>(frame).code)
                            .program.is_generator() ||
                        context.payload<CodeObject>(context.payload<FrameObject>(frame).code)
                            .program.is_coroutine()) {
                        detail::make_generator(context, frame, result);
                        if (raised(result)) {
                            activation.raising(result.exception());
                            break;
                        }
                        data.operands[base] = result.value();
                        data.trim_stack(context, base + 1);
                        data.pending_call.reset();
                    } else if (!activation.descend(frame)) {
                        raise_error(context, CoreObject::RecursionErrorType,
                                    U"maximum call depth exceeded", result);
                        activation.raising(result.exception());
                    }
                } else {
                    if (raised(result)) {
                        activation.raising(result.exception());
                        break;
                    }
                    data.operands[base] = result.value();
                    for (std::size_t i = base + 1; i < data.sp; ++i)
                        data.operands[i] = context.builtin(CoreObject::None);
                    data.sp = base + 1;
                    data.pending_call.reset();
                }
                break;
            }
            case Opcode::EnterRegion:
                data.regions.push_back({{operand, RegionPhase::Protected}, data.sp, {}});
                break;
            case Opcode::PrepareWith:
            case Opcode::PrepareAsyncWith: {
                RootScope roots(context);
                auto manager = roots.root(data.operands[data.sp - 1]);
                ReturnSlot enter(context);
                const bool asynchronous = opcode == Opcode::PrepareAsyncWith;
                with_method(context, manager, asynchronous ? Protocol::AEnter : Protocol::Enter,
                            enter);
                if (raised(enter)) {
                    activation.raising(enter.exception());
                    break;
                }
                with_method(context, manager, asynchronous ? Protocol::AExit : Protocol::Exit,
                            result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.operands[data.sp - 1] = result.value();
                data.operands[data.sp++] = enter.value();
                break;
            }
            case Opcode::EnterWith:
                data.regions.push_back({{operand, RegionPhase::Protected}, data.sp - 1, {}});
                break;
            case Opcode::WithExit: {
                const auto *error = std::get_if<ExceptionRaise>(&data.cleanups.back().saved_reason);
                data.operands[data.sp++] = data.operands[data.regions.back().stack_base - 1];
                data.operands[data.sp++] =
                    error ? context.type_of(error->exception) : context.builtin(CoreObject::None);
                data.operands[data.sp++] =
                    error ? error->exception : context.builtin(CoreObject::None);
                ReturnSlot traceback(context);
                if (error)
                    context.load(error->exception,
                                 context.payload<ExceptionObject>(error->exception).traceback,
                                 traceback);
                data.operands[data.sp++] = traceback.tag() == ResultTag::Ok
                                               ? traceback.value()
                                               : context.builtin(CoreObject::None);
                break;
            }
            case Opcode::WithResult: {
                if (std::holds_alternative<ExceptionRaise>(data.cleanups.back().saved_reason)) {
                    RootScope roots(context);
                    truth(context, roots.root(data.operands[data.sp - 1]), result);
                    if (raised(result)) {
                        activation.raising(result.exception());
                        break;
                    }
                    if (result.value() == context.builtin(CoreObject::True))
                        data.cleanups.back().saved_reason =
                            NormalExit{code.program.regions()[operand].after_pc,
                                       code.program.regions()[operand].parent};
                }
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            }
            case Opcode::LeaveRegion:
                data.reason = NormalExit{code.program.regions()[operand].normal_pc,
                                         code.program.regions()[operand].parent};
                break;
            case Opcode::EndHandler:
                data.finish_handler(context);
                activation.sync();
                break;
            case Opcode::BreakLoop:
                data.reason = LoopBreak{operand};
                break;
            case Opcode::ContinueLoop:
                data.reason = LoopContinue{operand};
                break;
            case Opcode::LoadException:
                data.operands[data.sp++] =
                    data.handled_exception.value_or(context.builtin(CoreObject::None));
                break;
            case Opcode::Reraise:
                if (operand) {
                    const auto exception = data.operands[data.sp - 1];
                    if (!kind<ExceptionObject, CoreObject::BaseExceptionType>(context, exception)) {
                        raise_error(context, CoreObject::TypeErrorType,
                                    U"exceptions must derive from BaseException", result);
                        activation.raising(result.exception());
                    } else
                        activation.raising(exception, false);
                } else if (data.handled_exception)
                    activation.raising(*data.handled_exception, false);
                else {
                    raise_error(context, CoreObject::RuntimeErrorType,
                                U"no active exception to reraise", result);
                    activation.raising(result.exception());
                }
                break;
            case Opcode::CheckEgMatch: {
                RootScope roots(context);
                ReturnSlot match(context), rest(context);
                detail::exception_group_match(context, roots.root(data.operands[data.sp - 2]),
                                              roots.root(data.operands[data.sp - 1]), match, rest);
                if (raised(match)) {
                    activation.raising(match.exception());
                    break;
                }
                data.operands[data.sp - 2] = rest.value();
                data.operands[data.sp - 1] = match.value();
                if (match.value() != context.builtin(CoreObject::None)) {
                    data.handled_exception = match.value();
                    data.handled_inherited = false;
                    activation.sync();
                }
                break;
            }
            case Opcode::PrepReraiseStar: {
                RootScope roots(context);
                detail::prepare_reraise_star(context, roots.root(data.operands[data.sp - 2]),
                                             roots.root(data.operands[data.sp - 1]), result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.operands[data.sp - 2] = result.value();
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            }
            case Opcode::ExceptionMatch:
                detail::exception_match(context, data.operands[data.sp - 2],
                                        data.operands[data.sp - 1], result);
                if (raised(result)) {
                    activation.raising(result.exception());
                    break;
                }
                data.operands[data.sp - 2] = result.value();
                data.operands[--data.sp] = context.builtin(CoreObject::None);
                break;
            case Opcode::MatchSequence:
            case Opcode::MatchMapping: {
                const auto type = context.type_of(data.operands[data.sp - 1]);
                const auto kind =
                    context.payload<TypeObject>(type).collection.load(std::memory_order_acquire);
                const auto expected = opcode == Opcode::MatchSequence ? CollectionKind::Sequence
                                                                      : CollectionKind::Mapping;
                data.operands[data.sp++] =
                    context.builtin(kind == expected ? CoreObject::True : CoreObject::False);
                break;
            }
            case Opcode::GetLen: {
                RootScope roots(context);
                length(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::MatchKeys: {
                RootScope roots(context);
                detail::match_keys(context, roots.root(data.operands[data.sp - 2]),
                                   roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp++] = result.value();
                break;
            }
            case Opcode::MatchClass: {
                RootScope roots(context);
                detail::match_class(context, roots.root(data.operands[data.sp - 3]),
                                    roots.root(data.operands[data.sp - 2]),
                                    roots.root(data.operands[data.sp - 1]), operand, result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.operands[data.sp - 3] = result.value();
                    data.trim_stack(context, data.sp - 2);
                }
                break;
            }
            case Opcode::CopyDictWithoutKeys: {
                RootScope roots(context);
                detail::match_mapping_rest(context, roots.root(data.operands[data.sp - 2]),
                                           roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else {
                    data.operands[data.sp - 2] = result.value();
                    data.operands[--data.sp] = context.builtin(CoreObject::None);
                }
                break;
            }
            case Opcode::GetAwaitable: {
                RootScope roots(context);
                detail::get_awaitable(context, roots.root(data.operands[data.sp - 1]), result);
                if (raised(result))
                    activation.raising(result.exception());
                else
                    data.operands[data.sp - 1] = result.value();
                break;
            }
            case Opcode::AsyncYieldValue: {
                RootScope roots(context);
                context.allocate_instance<AsyncGenWrappedValueObject>(
                    roots.root(context.builtin(CoreObject::AsyncGenWrappedValueType)), result,
                    data.operands[data.sp - 1]);
                data.operands[data.sp - 1] = result.value();
                [[fallthrough]];
            }
            case Opcode::YieldValue:
                out.ok(data.operands[data.sp - 1]);
                data.operands[data.sp - 1] = context.builtin(CoreObject::None);
                activation.suspend();
                return;
            case Opcode::ForAwait:
            case Opcode::YieldFrom: {
                const bool asynchronous_loop = opcode == Opcode::ForAwait;
                auto exhausted = [&] {
                    return asynchronous_loop && raised(result) &&
                           is_subtype(context, context.type_of(result.exception()),
                                      context.builtin(CoreObject::StopAsyncIterationType));
                };
                context.load(activation.current(), data.delegate, result);
                if (result.tag() == ResultTag::Empty) {
                    if (asynchronous_loop) {
                        RootScope roots(context);
                        detail::get_async_next(context, roots.root(data.operands[data.sp - 1]),
                                               result);
                        if (raised(result)) {
                            if (exhausted())
                                data.pc = operand;
                            else
                                activation.raising(result.exception());
                            break;
                        }
                    } else
                        result.ok(data.operands[data.sp - 1]);
                    context.store(activation.current(), data.delegate, result.value());
                    if (!asynchronous_loop)
                        data.operands[data.sp - 1] = context.builtin(CoreObject::None);
                }
                const bool throwing = data.resume_exception || data.resume_throw_args;
                const auto status = delegate_step(context, activation.current(), data, result);
                if (status == DelegateResult::Yielded) {
                    --data.pc;
                    out.ok(result.value());
                    activation.suspend();
                    return;
                }
                if (status == DelegateResult::Rejected) {
                    --data.pc;
                    out.raised(result.exception());
                    activation.suspend();
                    return;
                }
                if (status == DelegateResult::Raised) {
                    if (exhausted()) {
                        context.store(activation.current(), data.delegate, {});
                        data.pc = operand;
                    } else {
                        if (!asynchronous_loop)
                            data.trim_stack(context, data.sp - 1);
                        if (throwing && !data.handled_inherited)
                            detail::link_exception_context(context, result.exception());
                        activation.raising(result.exception());
                    }
                } else {
                    context.store(activation.current(), data.delegate, {});
                    if (asynchronous_loop)
                        data.operands[data.sp++] = result.value();
                    else
                        data.operands[data.sp - 1] = result.value();
                }
                break;
            }
            case Opcode::Count:
                throw std::logic_error("unverified opcode reached execution");
            }
        } catch (const std::bad_alloc &) {
            activation.raising(context.builtin(CoreObject::MemoryError));
        }
    }
}
void code_name(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).name);
}
void code_qualname(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).qualname);
}
void code_filename(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).filename);
}
void code_constants(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).constants);
}
void code_varnames(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).varnames);
}
void code_names(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).names);
}
void code_cellvars(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).cellvars);
}
void code_freevars(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<CodeObject>(value).freevars);
}
void code_stacksize(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, static_cast<std::int64_t>(c.payload<CodeObject>(value).program.max_stack()), out);
}
void code_firstline(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, c.payload<CodeObject>(value).program.first_line(), out);
}
void code_flags(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c, c.payload<CodeObject>(value).program.flags(), out);
}
void parameter_count(Attachment &c, Value value, std::optional<ParameterKind> kind, ReturnSlot &out)
{
    const auto &parameters = c.payload<CodeObject>(value).program.signature().parameters;
    const auto count =
        std::count_if(parameters.begin(), parameters.end(), [&](const auto &parameter) {
            return kind ? parameter.kind == *kind : parameter.kind != ParameterKind::KeywordOnly;
        });
    make_int(c, count, out);
}
void code_argcount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, {}, out);
}
void code_posonlycount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, ParameterKind::PositionalOnly, out);
}
void code_kwonlycount(Attachment &c, Value value, ReturnSlot &out)
{
    parameter_count(c, value, ParameterKind::KeywordOnly, out);
}
void code_nlocals(Attachment &c, Value value, ReturnSlot &out)
{
    make_int(c,
             static_cast<std::int64_t>(c.payload<CodeObject>(value).program.local_names().size()),
             out);
}
template <bool Lines>
void code_locations(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (c.type_of(self) != c.builtin(CoreObject::CodeType) ||
        !c.has_layout<CodeObject>(self.get(c))) {
        type_error(c, U"code position methods require a code object", out);
        return;
    }
    const auto positions = c.payload<CodeObject>(self).program.positions();
    std::vector<Value> rows;
    for (std::size_t start = 0; start < positions.size();) {
        RootScope row_roots(c);
        ReturnSlot value(c);
        std::vector<Value> fields;
        auto number = [&](std::optional<std::int64_t> n) {
            if (n)
                make_int(c, *n, value);
            else
                value.ok(c.builtin(CoreObject::None));
            if (raised(value)) {
                out.raised(value.exception());
                return false;
            }
            fields.push_back(row_roots.root(value.value()).get(c));
            return true;
        };
        const auto position = positions[start];
        auto end = start + 1;
        if constexpr (Lines) {
            while (end < positions.size() && positions[end].line == position.line) {
                if ((end & 4095) == 0)
                    c.poll();
                ++end;
            }
            if (!number(static_cast<std::int64_t>(start)) ||
                !number(static_cast<std::int64_t>(end)) ||
                !number(position.line ? std::optional<std::int64_t>{position.line} : std::nullopt))
                return;
        } else {
            for (const auto n :
                 {position.line, position.end_line, position.column, position.end_column})
                if (!number(position.line && n != std::numeric_limits<std::uint32_t>::max()
                                ? std::optional<std::int64_t>{n}
                                : std::nullopt))
                    return;
        }
        cxxpython::core::make_tuple(c, fields, value);
        if (raised(value)) {
            out.raised(value.exception());
            return;
        }
        rows.push_back(roots.root(value.value()).get(c));
        start = end;
        if ((start & 4095) == 0)
            c.poll();
    }
    // The existing tuple iterator owns the immutable snapshot, not borrowed Code data.
    ReturnSlot snapshot(c);
    cxxpython::core::make_tuple(c, rows, snapshot);
    if (raised(snapshot)) {
        out.raised(snapshot.exception());
        return;
    }
    get_iter(c, roots.root(snapshot.value()), out);
}
void frame_code(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<FrameObject>(value).code);
}
void frame_globals(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<FunctionObject>(c.payload<FrameObject>(value).function).globals);
}
void frame_builtins(Attachment &c, Value value, ReturnSlot &out)
{
    out.ok(c.payload<FunctionObject>(c.payload<FrameObject>(value).function).builtins);
}
void frame_back(Attachment &c, Value value, ReturnSlot &out)
{
    c.load(value, c.payload<FrameObject>(value).back, out);
    if (out.tag() == ResultTag::Empty)
        out.ok(c.builtin(CoreObject::None));
}
void frame_lineno(Attachment &c, Value value, ReturnSlot &out)
{
    const auto &frame = c.payload<FrameObject>(value);
    const auto &program = c.payload<CodeObject>(frame.code).program;
    const auto lasti = frame.last_instruction.load(std::memory_order_acquire);
    auto line =
        lasti != std::numeric_limits<std::size_t>::max() ? program.positions()[lasti].line : 0;
    make_int(c, line ? line : program.first_line(), out);
}
void frame_lasti(Attachment &c, Value value, ReturnSlot &out)
{
    const auto lasti =
        c.payload<FrameObject>(value).last_instruction.load(std::memory_order_acquire);
    make_int(
        c, lasti == std::numeric_limits<std::size_t>::max() ? -1 : static_cast<std::int64_t>(lasti),
        out);
}
std::u32string address(Value value)
{
    char buffer[2 * sizeof(std::uintptr_t)];
    auto end = std::to_chars(std::begin(buffer), std::end(buffer),
                             reinterpret_cast<std::uintptr_t>(value.object()), 16)
                   .ptr;
    return U"0x" + std::u32string(buffer, end);
}
void execution_repr(Attachment &c, Handle fn, std::span<const Value> args, ReturnSlot &out)
{
    const auto owner = c.payload<NativeFunctionObject>(fn).captures[0];
    if (c.type_of(args[0]) != owner) {
        type_error(c, U"execution repr requires its own object kind", out);
        return;
    }
    std::u32string result;
    if (owner == c.builtin(CoreObject::CodeType))
        result = U"<code object " + c.payload<StrObject>(c.payload<CodeObject>(args[0]).name).value;
    else
        result = U"<frame";
    result += U" at " + address(args[0]) + U">";
    make_str(c, std::move(result), out);
}
} // namespace
void make_function(Attachment &context, const FunctionSpec &spec, ReturnSlot &out)
{
    out.check_caller(context);
    RootScope roots(context);
    auto code = roots.root(spec.code.get(context));
    auto globals = roots.root(spec.globals.get(context));
    auto builtins = roots.root(spec.builtins ? spec.builtins->get(context)
                                             : context.builtin(CoreObject::Builtins));
    std::optional<Value> defaults, keywords, closure, annotations;
    if (spec.defaults)
        defaults = roots.root(spec.defaults->get(context)).get(context);
    if (spec.kwdefaults)
        keywords = roots.root(spec.kwdefaults->get(context)).get(context);
    if (spec.annotations)
        annotations = roots.root(spec.annotations->get(context)).get(context);
    if (spec.closure) {
        const auto value = roots.root(spec.closure->get(context)).get(context);
        if (value != context.builtin(CoreObject::None))
            closure = value;
    }
    out.reset(context);
    if (context.type_of(code) != context.builtin(CoreObject::CodeType) ||
        !context.has_layout<CodeObject>(code.get(context))) {
        type_error(context, U"function requires verified Code", out);
        return;
    }
    if (!kind<DictObject, CoreObject::DictType>(context, globals.get(context)) ||
        !kind<DictObject, CoreObject::DictType>(context, builtins.get(context)) ||
        (defaults && !kind<TupleObject, CoreObject::TupleType>(context, *defaults)) ||
        (keywords && !kind<DictObject, CoreObject::DictType>(context, *keywords)) ||
        (annotations && !kind<DictObject, CoreObject::DictType>(context, *annotations))) {
        type_error(context,
                   U"function requires globals/builtins dicts, defaults tuple and optional "
                   U"kwdefaults dict",
                   out);
        return;
    }
    const auto &program = context.payload<CodeObject>(code);
    if (!closure && !program.program.free_names().empty()) {
        type_error(context, U"function requires a closure tuple", out);
        return;
    }
    if (closure) {
        if (!kind<TupleObject, CoreObject::TupleType>(context, *closure)) {
            type_error(context, U"function closure must be a tuple", out);
            return;
        }
        const auto &cells = context.payload<TupleObject>(*closure).items;
        if (cells.size() != program.program.free_names().size()) {
            raise_error(context, CoreObject::ValueErrorType,
                        U"function closure length does not match Code free variables", out);
            return;
        }
        for (auto cell : cells)
            if (!kind<CellObject, CoreObject::CellType>(context, cell)) {
                type_error(context, U"function closure elements must be cells", out);
                return;
            }
    }
    const auto module = roots.root(detail::name_lookup(context, globals.get(context), U"__name__")
                                       .value_or(context.builtin(CoreObject::None)));
    context.allocate_instance<FunctionObject>(
        roots.root(context.builtin(CoreObject::FunctionType)), out, code.get(context),
        globals.get(context), defaults, keywords, program.name, program.qualname,
        builtins.get(context), closure, annotations, module.get(context), program.docstring);
}
namespace detail {
void frame_locals(Attachment &context, Handle frame, ReturnSlot &out)
{
    auto &data = context.payload<FrameObject>(frame);
    import_locals(context, data, context.payload<CodeObject>(data.code), out);
}

void execute_class_body(Attachment &context, Handle function, Handle mapping, ReturnSlot &out)
{
    if (!context.enter_call()) {
        raise_error(context, CoreObject::RecursionErrorType, U"maximum call depth exceeded", out);
        return;
    }
    try {
        ReturnSlot active(context);
        make_frame(context, function, {}, {}, active, mapping);
        if (raised(active))
            out.raised(active.exception());
        else {
            FrameActivation activation(context, active);
            run(context, activation, out);
        }
    } catch (...) {
        context.leave_call();
        throw;
    }
    context.leave_call();
}
void execute_function(Attachment &context, Handle function, CallArgsView args,
                      std::optional<Value> receiver, ReturnSlot &out)
{
    ReturnSlot active(context);
    make_frame(context, function, args, receiver, active);
    if (raised(active)) {
        out.raised(active.exception());
        return;
    }
    if (context.payload<CodeObject>(context.payload<FrameObject>(active.value()).code)
            .program.is_generator() ||
        context.payload<CodeObject>(context.payload<FrameObject>(active.value()).code)
            .program.is_coroutine()) {
        RootScope roots(context);
        make_generator(context, roots.root(active.value()), out);
        return;
    }
    FrameActivation activation(context, active);
    run(context, activation, out);
}
ResumeResult resume_generator_frame(Attachment &context, Handle frame, Handle sent,
                                    std::optional<Handle> thrown, ReturnSlot &out,
                                    std::optional<Handle> throw_args)
{
    ReturnSlot active(context);
    active.ok(frame.get(context));
    FrameActivation activation(context, active, true);
    auto &data = activation.frame();
    const auto lasti = data.last_instruction.load(std::memory_order_relaxed);
    const bool started = lasti != std::numeric_limits<std::size_t>::max();
    const bool delegating =
        started && delegation_opcode(activation.code().program.instructions()[lasti].opcode);
    if (delegating) {
        data.resume_value = sent.get(context);
        if (thrown)
            data.resume_exception = thrown->get(context);
        if (throw_args)
            data.resume_throw_args = throw_args->get(context);
    } else if (thrown) {
        if (started)
            data.trim_stack(context, data.sp - 1);
        if (!data.handled_inherited)
            link_exception_context(context, thrown->get(context));
        activation.raising(thrown->get(context));
    } else if (started)
        data.operands[data.sp - 1] = sent.get(context);
    run(context, activation, out);
    return activation.suspended() ? (raised(out) ? ResumeResult::Rejected : ResumeResult::Yielded)
           : raised(out)          ? ResumeResult::Raised
                                  : ResumeResult::Returned;
}
LayoutDescriptor execution_layout(CoreObject type)
{
    if (type == CoreObject::CodeType)
        return LayoutDescriptor::of<CodeObject>();
    if (type == CoreObject::FrameType)
        return LayoutDescriptor::of<FrameObject>();
    if (type == CoreObject::TracebackType)
        return LayoutDescriptor::of<TracebackObject>();
    if (type == CoreObject::FunctionType)
        return LayoutDescriptor::of<FunctionObject>();
    throw std::invalid_argument("not an execution object kind");
}
void install_execution(Attachment &context, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    auto field = [&](CoreObject kind, std::u32string name, NativeGetter getter,
                     NativeSetter setter = nullptr) {
        install_native_field(context, core, at(kind), std::move(name), getter, setter);
    };
    field(CoreObject::CodeType, U"co_name", code_name);
    field(CoreObject::CodeType, U"co_qualname", code_qualname);
    field(CoreObject::CodeType, U"co_filename", code_filename);
    field(CoreObject::CodeType, U"co_consts", code_constants);
    field(CoreObject::CodeType, U"co_varnames", code_varnames);
    field(CoreObject::CodeType, U"co_stacksize", code_stacksize);
    field(CoreObject::CodeType, U"co_firstlineno", code_firstline);
    field(CoreObject::CodeType, U"co_flags", code_flags);
    field(CoreObject::CodeType, U"co_argcount", code_argcount);
    field(CoreObject::CodeType, U"co_posonlyargcount", code_posonlycount);
    field(CoreObject::CodeType, U"co_kwonlyargcount", code_kwonlycount);
    field(CoreObject::CodeType, U"co_nlocals", code_nlocals);
    field(CoreObject::CodeType, U"co_names", code_names);
    field(CoreObject::CodeType, U"co_cellvars", code_cellvars);
    field(CoreObject::CodeType, U"co_freevars", code_freevars);
    install_function_descriptors(context, core);
    field(CoreObject::FrameType, U"f_code", frame_code);
    field(CoreObject::FrameType, U"f_back", frame_back);
    field(CoreObject::FrameType, U"f_globals", frame_globals);
    field(CoreObject::FrameType, U"f_builtins", frame_builtins);
    field(CoreObject::FrameType, U"f_lineno", frame_lineno);
    field(CoreObject::FrameType, U"f_lasti", frame_lasti);
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    install_native_attribute(context, core, at(CoreObject::CodeType), U"co_positions", unary,
                             code_locations<false>);
    install_native_attribute(context, core, at(CoreObject::CodeType), U"co_lines", unary,
                             code_locations<true>);
    for (auto kind : {CoreObject::CodeType, CoreObject::FrameType})
        install_native_attribute(context, core, at(kind), U"__repr__", unary, execution_repr,
                                 {at(kind).get(context)});
}
} // namespace detail
} // namespace cxxpython::core
