#include "cxxpython/core/execution.hpp"

#include <chrono>
#include <iostream>

namespace
{
    using namespace cxxpython::core;
    using Clock = std::chrono::steady_clock;

    struct Node
    {
        ObjectHeader header;
        GcEdge child;
        explicit Node(ObjectHeader head) : header(head) {}
        void trace(Tracer &tracer) const noexcept { tracer.visit(child); }
    };

    template <class F> std::int64_t measure(F &&function)
    {
        auto start = Clock::now();
        function();
        return std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    }
    void identity(Attachment &, Handle, std::span<const Value> args, ReturnSlot &out)
    { out.ok(args[0]); }
}

int main()
{
    using namespace cxxpython::core;
    constexpr int iterations = 100000;
    Interpreter interpreter;
    Attachment context(interpreter);
    auto allocation_ns = measure([&] {
        for (int i = 0; i < iterations; ++i)
        {
            ReturnSlot out(context);
            context.allocate<Node>(out);
        }
    });
    context.collect();
    if (context.stats().objects != 0)
        return 1;

    RootScope scope(context);
    ReturnSlot value(context);
    context.allocate<Node>(value);
    auto root_ns = measure([&] {
        for (int i = 0; i < iterations; ++i)
        {
            auto root = scope.root(value.result().value());
            scope.release(root);
        }
    });
    value.reset();
    context.collect();
    auto stats = context.stats();
    if (stats.objects != 0 || stats.roots != 1 || stats.root_cells > 2)
        return 1;
    std::cout << "iterations=" << iterations
              << " header_bytes=" << sizeof(ObjectHeader)
              << " node_bytes=" << sizeof(Node)
              << " allocation_and_auto_gc_ns=" << allocation_ns
              << " root_register_release_ns=" << root_ns
              << " collections=" << stats.collections
              << " live_bytes=" << stats.live_bytes
              << " recorded_metadata_bytes=" << stats.metadata_bytes
              << " root_cells=" << stats.root_cells
              << " last_gc_pause_ns=" << stats.last_pause_ns << '\n';

    context.bootstrap_core();
    SignatureLayout signature;
    signature.parameters = {{U"value"}};
    make_native_function(context, "identity", signature, identity, {}, value);
    auto function = scope.root(value.result().value());
    std::array args{context.builtin(CoreObject::None)};
    const auto native_call_ns = measure([&] {
        for (int i = 0; i < iterations; ++i) call(context, function, {args, {}}, value);
    });
    if (value.result().tag() != ResultTag::Ok || value.result().value() != args[0]) return 1;

    create_type(context, {"Receiver"}, value);
    auto cls = scope.root(value.result().value());
    make_str(context, U"method", value);
    auto name = scope.root(value.result().value());
    set_attribute(context, cls, name, function, value);
    if (value.result().tag() != ResultTag::Ok) return 1;
    context.allocate_instance<InstanceObject>(cls, value);
    auto receiver = scope.root(value.result().value());
    constexpr int method_iterations = 10000;
    const auto method_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
        {
            get_attribute(context, receiver, name, value);
            auto method = scope.root(value.result().value());
            call(context, method, {}, value);
            scope.release(method);
        }
    });
    if (value.result().tag() != ResultTag::Ok || value.result().value() != receiver.get(context)) return 1;
    context.collect();
    std::cout << "native_call_iterations=" << iterations << " native_call_ns=" << native_call_ns
              << " method_iterations=" << method_iterations << " lookup_bind_call_ns=" << method_ns
              << " live_objects=" << context.stats().objects << '\n';

    TypeSpec slots{"Slotted"}; slots.slots = std::vector<std::u32string>{U"x", U"y"};
    create_type(context, slots, value);
    auto slotted_type = scope.root(value.result().value());
    context.allocate_instance<InstanceObject>(slotted_type, value);
    auto slotted = scope.root(value.result().value());
    make_str(context, U"x", value); auto slot_name = scope.root(value.result().value());
    auto none = scope.root(context.builtin(CoreObject::None));
    bool valid = true;
    const auto slot_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
        {
            if (set_attribute(context, slotted, slot_name, none, value).tag() != ResultTag::Ok ||
                get_attribute(context, slotted, slot_name, value).tag() != ResultTag::Ok ||
                value.result().value() != none.get(context))
            { valid = false; return; }
        }
    });
    if (!valid) return 1;
    std::cout << "slot_iterations=" << method_iterations << " slot_set_get_ns=" << slot_ns
              << " slot_instance_bytes=" << context.payload<TypeObject>(slotted_type).instance_layout.storage_size()
              << " generic_prefix_bytes=" << sizeof(InstanceObject) << '\n';

    make_int(context, 40, value); auto left = scope.root(value.result().value());
    make_int(context, 2, value); auto right = scope.root(value.result().value());
    make_int_decimal(context, "18446744073709551616", value); auto large = scope.root(value.result().value());
    value.reset(); context.collect(); const auto before_numbers = context.stats().objects;
    const auto integer_ns = measure([&] {
        for (int i = 0; i < iterations; ++i)
            if (binary(context, left, right, BinaryOp::Add, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || int_decimal(context, value.result().value()) != "42") return 1;
    const auto big_integer_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
            if (binary(context, large, large, BinaryOp::Multiply, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || int_decimal(context, value.result().value()) != "340282366920938463463374607431768211456") return 1;
    value.reset(); context.collect();
    if (context.stats().objects != before_numbers) return 1;
    std::cout << "int_iterations=" << iterations << " int_add_ns=" << integer_ns
              << " big_int_iterations=" << method_iterations << " big_int_mul_ns=" << big_integer_ns
              << " int_object_bytes=" << sizeof(IntObject) << " numeric_live_set_check=ok\n";

    make_str(context, U"hash sample \U0001f600", value); auto hash_input = scope.root(value.result().value());
    cxxpython::core::hash(context, hash_input, value);
    if (value.result().tag() != ResultTag::Ok) return 1;
    std::int64_t expected_hash = 0;
    if (!int_fits_signed(context, value.result().value(), expected_hash)) return 1;
    std::array tuple_items{left.get(context), right.get(context)};
    cxxpython::core::make_tuple(context, tuple_items, value); auto repr_input = scope.root(value.result().value());
    value.reset(); context.collect(); const auto before_representation = context.stats().objects;
    const auto hash_ns = measure([&] {
        for (int i = 0; i < iterations; ++i)
            if (cxxpython::core::hash(context, hash_input, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    std::int64_t actual_hash = 0;
    if (!valid || !int_fits_signed(context, value.result().value(), actual_hash) || actual_hash != expected_hash) return 1;
    const auto repr_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
            if (repr(context, repr_input, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || context.payload<StrObject>(value.result().value()).value != U"(40, 2)") return 1;
    value.reset(); context.collect();
    if (context.stats().objects != before_representation) return 1;
    std::cout << "hash_iterations=" << iterations << " string_hash_ns=" << hash_ns
              << " repr_iterations=" << method_iterations << " tuple_repr_ns=" << repr_ns
              << " representation_live_set_check=ok\n";

    CodeSpec code_spec; code_spec.name = U"add";
    code_spec.signature.parameters = {{U"left"}, {U"right"}}; code_spec.locals = {U"left", U"right"};
    CodeBuilder builder(context, code_spec); builder.emit(Opcode::LoadFast, 0); builder.emit(Opcode::LoadFast, 1);
    builder.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); builder.emit(Opcode::Return);
    builder.freeze(value); auto verified = scope.root(value.result().value());
    context.allocate_instance<DictObject>(scope.root(context.builtin(CoreObject::DictType)), value);
    auto globals = scope.root(value.result().value());
    make_function(context, {verified, globals}, value); auto python_function = scope.root(value.result().value());
    value.reset(); context.collect(); const auto before_execution = context.stats().objects;
    const auto execution_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
            if (call(context, python_function, {tuple_items, {}}, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || int_decimal(context, value.result().value()) != "42") return 1;
    value.reset(); context.collect();
    if (context.stats().objects != before_execution) return 1;
    std::cout << "function_iterations=" << method_iterations << " verified_function_ns=" << execution_ns
              << " verified_instructions=" << context.payload<CodeObject>(verified).program.instructions().size()
              << " max_stack=" << context.payload<CodeObject>(verified).program.max_stack()
              << " frame_prefix_bytes=" << sizeof(FrameObject) << " execution_live_set_check=ok\n";

    CodeSpec caller_spec = code_spec; caller_spec.name = U"call_add"; CodeBuilder caller(context, caller_spec);
    caller.emit(Opcode::LoadConst, caller.constant(python_function)); caller.emit(Opcode::LoadFast, 0); caller.emit(Opcode::LoadFast, 1);
    caller.call(2); caller.emit(Opcode::Return); caller.freeze(value); auto caller_code = scope.root(value.result().value());
    make_function(context, {caller_code, globals}, value); auto scheduled = scope.root(value.result().value());
    value.reset(); context.collect(); const auto before_scheduled = context.stats().objects;
    const auto scheduled_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
            if (call(context, scheduled, {tuple_items, {}}, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || int_decimal(context, value.result().value()) != "42") return 1;
    value.reset(); context.collect();
    if (context.stats().objects != before_scheduled) return 1;
    std::cout << "scheduled_iterations=" << method_iterations << " scheduled_function_ns=" << scheduled_ns
              << " caller_instructions=" << context.payload<CodeObject>(caller_code).program.instructions().size()
              << " caller_max_stack=" << context.payload<CodeObject>(caller_code).program.max_stack()
              << " callee_instructions=" << context.payload<CodeObject>(verified).program.instructions().size()
              << " scheduled_live_set_check=ok\n";

    CodeBuilder cleanup(context, code_spec); auto region = cleanup.region(RegionKind::Finally);
    cleanup.enter(region); cleanup.emit(Opcode::LoadFast, 0); cleanup.emit(Opcode::LoadFast, 1);
    cleanup.emit(Opcode::Binary, static_cast<std::uint32_t>(BinaryOp::Add)); cleanup.emit(Opcode::Return); cleanup.leave(region);
    cleanup.handler(region); cleanup.emit(Opcode::LoadConst, cleanup.constant(function));
    cleanup.emit(Opcode::LoadConst, cleanup.constant(none)); cleanup.call(1); cleanup.emit(Opcode::PopTop); cleanup.end_handler(region);
    cleanup.freeze(value); auto cleanup_code = scope.root(value.result().value());
    make_function(context, {cleanup_code, globals}, value); auto cleanup_function = scope.root(value.result().value());
    value.reset(); context.collect(); const auto before_cleanup = context.stats().objects;
    const auto cleanup_ns = measure([&] {
        for (int i = 0; i < method_iterations; ++i)
            if (call(context, cleanup_function, {tuple_items, {}}, value).tag() != ResultTag::Ok) { valid = false; return; }
    });
    if (!valid || int_decimal(context, value.result().value()) != "42") return 1;
    value.reset(); context.collect();
    if (context.stats().objects != before_cleanup) return 1;
    std::cout << "cleanup_iterations=" << method_iterations << " cleanup_function_ns=" << cleanup_ns
              << " cleanup_instructions=" << context.payload<CodeObject>(cleanup_code).program.instructions().size()
              << " cleanup_max_stack=" << context.payload<CodeObject>(cleanup_code).program.max_stack()
              << " active_region_bytes=" << sizeof(ActiveRegion) << " cleanup_continuation_bytes=" << sizeof(CleanupContinuation)
              << " cleanup_live_set_check=ok\n";
}
