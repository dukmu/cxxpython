#include "cxxpython/core/execution.hpp"

#include <unordered_set>

namespace cxxpython::core
{
    namespace
    {
        void trace_reason(const TracedReason &reason, Tracer &tracer) noexcept
        {
            if (const auto *returned = std::get_if<FunctionReturn>(&reason)) tracer.visit(returned->value);
            else if (const auto *raised = std::get_if<ExceptionRaise>(&reason)) tracer.visit(raised->exception);
        }
    }
    void FrameObject::trace(Tracer &tracer) const noexcept
    {
        tracer.visit(function); tracer.visit(code); if (back) tracer.visit(*back);
        for (const auto &value : locals) if (value) tracer.visit(*value);
        for (auto cell : cells) tracer.visit(cell);
        for (std::size_t i = 0; i < sp; ++i) tracer.visit(operands[i]);
        if (handled_exception) tracer.visit(*handled_exception);
        if (reason) trace_reason(*reason, tracer);
        for (const auto &region : regions) if (region.previous_exception) tracer.visit(*region.previous_exception);
        for (const auto &continuation : cleanups)
        {
            trace_reason(continuation.saved_reason, tracer);
            if (continuation.previous_exception) tracer.visit(*continuation.previous_exception);
        }
    }
    void FrameObject::trim_stack(Attachment &context, std::size_t size) noexcept
    {
        while (sp > size) operands[--sp] = context.builtin(CoreObject::None);
    }
    bool FrameObject::unwind(Attachment &context)
    {
        const auto layouts = context.payload<CodeObject>(code).program.regions();
        for (;;)
        {
            const auto top = regions.empty() ? std::optional<RegionToken>{} : regions.back().token;
            if (const auto *normal = std::get_if<NormalExit>(&*reason); normal && normal->stop == top)
            { pc = normal->pc; reason.reset(); return true; }
            if (!top) return false;
            const auto active = regions.back(); const auto &layout = layouts[top->region];
            trim_stack(context, active.stack_base);
            if (top->phase == RegionPhase::Protected)
            {
                if (layout.kind == RegionKind::Finally || layout.kind == RegionKind::With)
                {
                    cleanups.push_back({top->region, *reason, handled_exception});
                    regions.back().token.phase = RegionPhase::Cleanup;
                    if (const auto *raised = std::get_if<ExceptionRaise>(&*reason)) handled_exception = raised->exception;
                    pc = layout.handler_pc; reason.reset(); return true;
                }
                if (const auto *raised = std::get_if<ExceptionRaise>(&*reason); raised && layout.kind == RegionKind::Except)
                {
                    regions.back().token.phase = RegionPhase::Handler; regions.back().previous_exception = handled_exception;
                    handled_exception = raised->exception; operands[sp++] = raised->exception;
                    pc = layout.handler_pc; reason.reset(); return true;
                }
                if (layout.kind == RegionKind::Loop)
                {
                    if (const auto *broken = std::get_if<LoopBreak>(&*reason); broken && broken->region == top->region)
                    { regions.pop_back(); pc = layout.break_pc; reason.reset(); return true; }
                    if (const auto *continued = std::get_if<LoopContinue>(&*reason); continued && continued->region == top->region)
                    { pc = layout.continue_pc; reason.reset(); return true; }
                }
            }
            if (top->phase == RegionPhase::Handler) handled_exception = active.previous_exception;
            else if (top->phase == RegionPhase::Cleanup)
            { handled_exception = cleanups.back().previous_exception; cleanups.pop_back(); }
            if (layout.kind == RegionKind::With) trim_stack(context, active.stack_base - 1);
            regions.pop_back();
        }
    }
    void FrameObject::finish_handler(Attachment &context)
    {
        const auto active = regions.back(); const auto &layout = context.payload<CodeObject>(code).program.regions()[active.token.region];
        trim_stack(context, active.stack_base); regions.pop_back();
        if (active.token.phase == RegionPhase::Handler)
        { handled_exception = active.previous_exception; pc = layout.after_pc; }
        else
        {
            reason = cleanups.back().saved_reason; handled_exception = cleanups.back().previous_exception; cleanups.pop_back();
            if (layout.kind == RegionKind::With) trim_stack(context, active.stack_base - 1);
        }
    }
    namespace detail
    {
        void link_exception_context(Attachment &context, Value exception)
        {
            auto handled = context.handled_exception();
            if (!handled || *handled == exception || exception == context.builtin(CoreObject::MemoryError)) return;
            RootScope roots(context); roots.root(exception); roots.root(*handled);
            for (;;)
            {
                const auto epoch = context.publication_epoch(PublicationDomain::Exception);
                if (!(epoch & 1))
                {
                    RootScope chain(context); ReturnSlot link(context); std::unordered_set<ObjectHeader *> seen;
                    std::optional<Value> cut;
                    for (auto cursor = handled; cursor;)
                    {
                        chain.root(*cursor);
                        if (!seen.insert(cursor->object()).second) break;
                        context.load(*cursor, context.payload<ExceptionObject>(*cursor).context, link);
                        const auto next = link.tag() == ResultTag::Ok ? std::optional<Value>{link.value()} : std::optional<Value>{};
                        if (next == exception) { cut = cursor; break; }
                        cursor = next; if (seen.size() % 64 == 0) context.poll();
                    }
                    NoSafepoint commit(context);
                    if (context.begin_publication(PublicationDomain::Exception, epoch))
                    {
                        if (cut) context.store(*cut, context.payload<ExceptionObject>(*cut).context, {});
                        context.store(exception, context.payload<ExceptionObject>(exception).context, handled);
                        context.end_publication(PublicationDomain::Exception, epoch); return;
                    }
                }
                context.poll(); std::this_thread::yield();
            }
        }
        void exception_match(Attachment &context, Value exception, Value matcher, ReturnSlot &out)
        {
            if (!context.has_layout<ExceptionObject>(exception) ||
                !is_subtype(context, context.type_of(exception), context.builtin(CoreObject::BaseExceptionType)))
            { raise_error(context, CoreObject::TypeErrorType, U"EXCEPTION_MATCH requires a BaseException instance", out); return; }
            auto valid = [&](Value type) {
                return context.has_layout<TypeObject>(type) && is_subtype(context, context.type_of(type), context.type_type()) &&
                       is_subtype(context, type, context.builtin(CoreObject::BaseExceptionType));
            };
            const bool tuple = context.has_layout<TupleObject>(matcher) &&
                is_subtype(context, context.type_of(matcher), context.builtin(CoreObject::TupleType));
            bool matched = false;
            if (tuple)
            {
                for (auto type : context.payload<TupleObject>(matcher).items)
                {
                    if (!valid(type)) { raise_error(context, CoreObject::TypeErrorType, U"catching classes must derive from BaseException", out); return; }
                    matched |= is_subtype(context, context.type_of(exception), type);
                }
            }
            else
            {
                if (!valid(matcher)) { raise_error(context, CoreObject::TypeErrorType, U"catching classes must derive from BaseException", out); return; }
                matched = is_subtype(context, context.type_of(exception), matcher);
            }
            out.ok(context.builtin(matched ? CoreObject::True : CoreObject::False));
        }
    }
}
