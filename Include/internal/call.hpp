#pragma once

#include "cxxpython/objects.hpp"

namespace cxxpython::core::detail {
struct FunctionDefaults {
    std::span<const Value> positional;
    std::optional<Value> keywords;
};
struct PreparedPythonCall {
    Handle function;
    std::optional<Handle> receiver;
    std::vector<Value> positional;
    std::vector<KeywordArg> keywords;
    std::optional<CallArgsView> borrowed_arguments;
    CallArgsView arguments() const noexcept
    {
        return borrowed_arguments.value_or(CallArgsView{positional, keywords});
    }
};
// A prepared target borrows the caller's RootScope; otherwise out owns
// the completed value/exception. No prepared target is a Python value.
std::optional<PreparedPythonCall> prepare_call(Attachment &context, Value callable,
                                               CallArgsView args, RootScope &roots,
                                               ReturnSlot &out);
// VM inputs and argument buffers remain live through immediate frame creation.
// Temporary receiver-prefix buffers are still copied by ordinary dispatch.
std::optional<PreparedPythonCall> prepare_frame_call(Attachment &, Value, CallArgsView, RootScope &,
                                                     ReturnSlot &);
bool bind_arguments(Attachment &context, const SignatureLayout &signature, CallArgsView args,
                    std::optional<Value> receiver, RootScope &roots, std::vector<Value> &bound,
                    ReturnSlot &out, std::optional<FunctionDefaults> defaults = {});
// Locals must be empty slots sized for the entire frame, including *args/**kwargs. The caller
// roots arguments and receiver through frame creation; locals are not themselves GC roots.
bool bind_frame_arguments(Attachment &context, const SignatureLayout &signature, CallArgsView args,
                          std::optional<Value> receiver, RootScope &roots,
                          std::vector<std::optional<Value>> &locals, ReturnSlot &out,
                          std::optional<FunctionDefaults> defaults = {});
} // namespace cxxpython::core::detail
