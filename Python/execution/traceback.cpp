#include "cxxpython/core/execution.hpp"

#include <limits>
#include <unordered_set>

namespace cxxpython::core
{
    namespace
    {
        void type_error(Attachment &context, std::u32string message, ReturnSlot &out)
        { raise_error(context, CoreObject::TypeErrorType, std::move(message), out); }
        bool traceback_or_none(Attachment &context, Value value, ReturnSlot &out)
        {
            if (value == context.builtin(CoreObject::None) ||
                (context.type_of(value) == context.builtin(CoreObject::TracebackType) && context.has_layout<TracebackObject>(value))) return true;
            type_error(context, U"expected a traceback object or None", out); return false;
        }
        std::optional<Value> optional_traceback(Attachment &context, Value value)
        { return value == context.builtin(CoreObject::None) ? std::optional<Value>{} : value; }
        void exception_traceback(Attachment &context, Value value, ReturnSlot &out)
        { context.load(value, context.payload<ExceptionObject>(value).traceback, out); if (out.tag() == ResultTag::Empty) out.ok(context.builtin(CoreObject::None)); }
        void set_exception_traceback(Attachment &context, Value self, std::optional<Value> value, ReturnSlot &out)
        {
            if (!value) { type_error(context, U"cannot delete __traceback__", out); return; }
            if (!traceback_or_none(context, *value, out)) return;
            context.store(self, context.payload<ExceptionObject>(self).traceback, optional_traceback(context, *value));
            out.ok(context.builtin(CoreObject::None));
        }
        void with_traceback(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (!context.has_layout<ExceptionObject>(args[0]) ||
                !is_subtype(context, context.type_of(args[0]), context.builtin(CoreObject::BaseExceptionType)))
            { type_error(context, U"with_traceback requires a BaseException instance", out); return; }
            set_exception_traceback(context, args[0], args[1], out);
            if (out.tag() == ResultTag::Ok) out.ok(args[0]);
        }
        void traceback_next(Attachment &context, Value value, ReturnSlot &out)
        { context.load(value, context.payload<TracebackObject>(value).next, out); if (out.tag() == ResultTag::Empty) out.ok(context.builtin(CoreObject::None)); }
        void set_traceback_next(Attachment &context, Value self, std::optional<Value> value, ReturnSlot &out)
        {
            if (!value) { type_error(context, U"cannot delete tb_next", out); return; }
            if (!traceback_or_none(context, *value, out)) return;
            const auto next = optional_traceback(context, *value);
            RootScope roots(context); roots.root(self); if (next) roots.root(*next);
            for (;;)
            {
                const auto epoch = context.publication_epoch(PublicationDomain::Exception);
                bool cycle = false;
                if (!(epoch & 1))
                {
                    RootScope chain(context); ReturnSlot link(context); std::unordered_set<ObjectHeader *> seen;
                    for (auto cursor = next; cursor;)
                    {
                        chain.root(*cursor);
                        if (*cursor == self || !seen.insert(cursor->object()).second) { cycle = true; break; }
                        context.load(*cursor, context.payload<TracebackObject>(*cursor).next, link);
                        cursor = link.tag() == ResultTag::Ok ? std::optional<Value>{link.value()} : std::optional<Value>{};
                        if (seen.size() % 64 == 0) context.poll();
                    }
                    if (context.publication_epoch(PublicationDomain::Exception) == epoch)
                    {
                        if (cycle) { raise_error(context, CoreObject::ValueErrorType, U"traceback loop detected", out); return; }
                        NoSafepoint commit(context);
                        if (context.begin_publication(PublicationDomain::Exception, epoch))
                        {
                            context.store(self, context.payload<TracebackObject>(self).next, next);
                            context.end_publication(PublicationDomain::Exception, epoch);
                            out.ok(context.builtin(CoreObject::None)); return;
                        }
                    }
                }
                context.poll(); std::this_thread::yield();
            }
        }
        void traceback_frame(Attachment &context, Value value, ReturnSlot &out)
        { out.ok(context.payload<TracebackObject>(value).frame); }
        void traceback_lasti(Attachment &context, Value value, ReturnSlot &out)
        { make_int(context, context.payload<TracebackObject>(value).lasti, out); }
        void traceback_lineno(Attachment &context, Value value, ReturnSlot &out)
        {
            const auto &data = context.payload<TracebackObject>(value);
            if (data.lineno != -1) { make_int(context, data.lineno, out); return; }
            const auto &frame = context.payload<FrameObject>(data.frame);
            const auto &program = context.payload<CodeObject>(frame.code).program;
            if (data.lasti < 0 || static_cast<std::uint64_t>(data.lasti) >= program.positions().size())
            { out.ok(context.builtin(CoreObject::None)); return; }
            const auto line = program.positions()[data.lasti].line;
            make_int(context, line ? line : program.first_line(), out);
        }
        bool position(Attachment &context, Value value, std::int64_t &converted, ReturnSlot &out)
        {
            RootScope roots(context); ReturnSlot result(context); index(context, roots.root(value), result);
            if (result.tag() == ResultTag::Raised) { out.raised(result.exception()); return false; }
            if (!int_fits_signed(context, result.value(), converted) ||
                converted < std::numeric_limits<std::int32_t>::min() || converted > std::numeric_limits<std::int32_t>::max())
            { raise_error(context, CoreObject::OverflowErrorType, U"traceback position does not fit a signed 32-bit integer", out); return false; }
            return true;
        }
        void traceback_new(Attachment &context, Handle, std::span<const Value> args, ReturnSlot &out)
        {
            if (args[0] != context.builtin(CoreObject::TracebackType))
            { type_error(context, U"traceback.__new__ requires the traceback type", out); return; }
            if (!traceback_or_none(context, args[1], out)) return;
            if (context.type_of(args[2]) != context.builtin(CoreObject::FrameType) || !context.has_layout<FrameObject>(args[2]))
            { type_error(context, U"traceback requires a frame object", out); return; }
            std::int64_t lasti, lineno;
            if (!position(context, args[3], lasti, out) || !position(context, args[4], lineno, out)) return;
            RootScope roots(context);
            context.allocate_instance<TracebackObject>(roots.root(args[0]), out, optional_traceback(context, args[1]), args[2], lasti, lineno);
        }
    }
    namespace detail
    {
        void append_traceback(Attachment &context, Handle exception, Handle frame, ReturnSlot &out)
        {
            out.check_caller(context);
            const auto error = exception.get(context); const auto active = frame.get(context);
            validate_exception(context, error);
            // The shared emergency object must not retain an arbitrary execution graph.
            if (error == context.builtin(CoreObject::MemoryError)) { out.raised(error); return; }
            try
            {
                RootScope roots(context); auto rooted_error = roots.root(error); auto rooted_frame = roots.root(active);
                auto type = roots.root(context.builtin(CoreObject::TracebackType)); ReturnSlot node(context), previous(context);
                const auto &data = context.payload<FrameObject>(rooted_frame);
                const auto &program = context.payload<CodeObject>(data.code).program;
                const auto instruction = data.last_instruction.load(std::memory_order_acquire);
                const auto lasti = instruction != std::numeric_limits<std::size_t>::max() ? static_cast<std::int64_t>(instruction) : -1;
                const auto line = lasti >= 0 ? program.positions()[instruction].line : 0;
                for (;;)
                {
                    context.load(error, context.payload<ExceptionObject>(rooted_error).traceback, previous);
                    const auto head = previous.tag() == ResultTag::Ok ? std::optional<Value>{previous.value()} : std::optional<Value>{};
                    context.allocate_instance<TracebackObject>(type, node, head, active, lasti, static_cast<std::int64_t>(line ? line : program.first_line()));
                    if (context.compare_exchange(error, context.payload<ExceptionObject>(rooted_error).traceback, head, node.value())) break;
                    context.poll();
                }
                out.raised(error);
            }
            catch (const std::bad_alloc &) { out.raised(context.builtin(CoreObject::MemoryError)); }
        }
        void install_exception_runtime(Attachment &context, std::span<const Handle> core)
        {
            auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
            auto field = [&](CoreObject kind, std::u32string name, NativeGetter getter, NativeSetter setter = nullptr) {
                RootScope roots(context); ReturnSlot out(context);
                context.allocate_instance<GetSetDescriptorObject>(at(CoreObject::GetSetDescriptorType), out, at(kind).get(context), getter, setter);
                auto descriptor = roots.root(out.value());
                context.allocate_instance<StrObject>(at(CoreObject::StrType), out, std::move(name));
                name_store(context, *TypeView(context, at(kind)).dictionary(), out.value(), descriptor.get(context));
            };
            field(CoreObject::BaseExceptionType, U"__traceback__", exception_traceback, set_exception_traceback);
            field(CoreObject::TracebackType, U"tb_next", traceback_next, set_traceback_next);
            field(CoreObject::TracebackType, U"tb_frame", traceback_frame);
            field(CoreObject::TracebackType, U"tb_lasti", traceback_lasti);
            field(CoreObject::TracebackType, U"tb_lineno", traceback_lineno);
            SignatureLayout method; method.parameters = {{U"self", ParameterKind::PositionalOnly}, {U"tb", ParameterKind::PositionalOnly}};
            install_native_attribute(context, core, at(CoreObject::BaseExceptionType), U"with_traceback", method, with_traceback);
            SignatureLayout constructor;
            constructor.parameters = {{U"cls", ParameterKind::PositionalOnly}, {U"tb_next"}, {U"tb_frame"}, {U"tb_lasti"}, {U"tb_lineno"}};
            install_native_attribute(context, core, at(CoreObject::TracebackType), U"__new__", constructor, traceback_new);
            RootScope roots(context); ReturnSlot out(context);
            auto dict = *TypeView(context, at(CoreObject::TracebackType)).dictionary();
            context.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), out, *name_lookup(context, dict, U"__new__"));
            auto wrapper = roots.root(out.value());
            context.allocate_instance<StrObject>(at(CoreObject::StrType), out, U"__new__");
            name_store(context, dict, out.value(), wrapper.get(context));
        }
    }
}
