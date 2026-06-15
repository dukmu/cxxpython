#include "cxxpython/bytecode.hpp"
#include "cxxpython/runtime.hpp"

#include "../Objects/type_initialization.hpp"

#include <algorithm>
#include <optional>
#include <queue>
#include <sstream>
#include <utility>
#include <vector>

namespace cxxpython
{
    BytecodeError::BytecodeError(std::string message)
        : message_(std::move(message))
    {
    }

    const char *BytecodeError::what() const noexcept
    {
        return message_.c_str();
    }

    namespace
    {
        CodeObject *require_code_instance(Runtime &runtime, Object *instance, const char *name)
        {
            auto *code = dynamic_cast<CodeObject *>(instance);
            if (!code)
                raise_type_error(&runtime, std::string(name) + " descriptor receiver does not have code layout");
            return code;
        }

        Object *code_name_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_code_instance(runtime, instance, "co_name")->name());
        }

        Object *code_filename_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_str(require_code_instance(runtime, instance, "co_filename")->filename());
        }

        Object *code_argcount_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_argcount")->arg_count()));
        }

        Object *code_posonlyargcount_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_posonlyargcount")->posonly_arg_count()));
        }

        Object *code_kwonlyargcount_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_kwonlyargcount")->kwonly_arg_count()));
        }

        Object *code_flags_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_flags")->flags()));
        }

        Object *code_stacksize_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return runtime.new_int(static_cast<std::int64_t>(
                require_code_instance(runtime, instance, "co_stacksize")->stack_size()));
        }

        Object *tuple_from_object_vector(Runtime &runtime, const std::vector<Object *> &items)
        {
            std::vector<Object *> copied(items.begin(), items.end());
            return runtime.new_tuple(std::move(copied));
        }

        Object *tuple_from_str_vector(Runtime &runtime, const std::vector<StrObject *> &items)
        {
            std::vector<Object *> copied(items.begin(), items.end());
            return runtime.new_tuple(std::move(copied));
        }

        Object *code_consts_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_object_vector(runtime, require_code_instance(runtime, instance, "co_consts")->constants());
        }

        Object *code_names_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_str_vector(runtime, require_code_instance(runtime, instance, "co_names")->names());
        }

        Object *code_varnames_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_str_vector(runtime, require_code_instance(runtime, instance, "co_varnames")->varnames());
        }

        Object *code_cellvars_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_str_vector(runtime, require_code_instance(runtime, instance, "co_cellvars")->cellvars());
        }

        Object *code_freevars_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            return tuple_from_str_vector(runtime, require_code_instance(runtime, instance, "co_freevars")->freevars());
        }

        Object *code_firstlineno_getter(Runtime &runtime, Object *instance, TypeObject *)
        {
            CodeObject *code = require_code_instance(runtime, instance, "co_firstlineno");
            std::uint32_t line = 0;
            const std::vector<std::uint32_t> &lines = code->lines();
            if (!lines.empty())
                line = lines.front();
            return runtime.new_int(static_cast<std::int64_t>(line));
        }

        std::string code_repr(const Object &object)
        {
            auto *code = static_cast<const CodeObject *>(&object);
            std::ostringstream out;
            out << "<code object " << code->name()
                << " at " << &object
                << ", file '" << code->filename() << "'>";
            return out.str();
        }

        std::int64_t code_hash(const Object &object)
        {
            return reinterpret_cast<std::intptr_t>(&object);
        }

        bool code_equal(const Object &left, const Object &right)
        {
            return &left == &right;
        }

        bool code_truth(const Object &object)
        {
            (void)object;
            return true;
        }

        void code_traverse(const Object &object, const VisitChildren &visitor)
        {
            auto *code = static_cast<const CodeObject *>(&object);
            for (Object *constant : code->constants())
            {
                if (constant)
                    visitor(constant);
            }
            for (StrObject *name : code->names())
            {
                if (name)
                    visitor(name);
            }
            for (StrObject *varname : code->varnames())
            {
                if (varname)
                    visitor(varname);
            }
            for (StrObject *cellvar : code->cellvars())
            {
                if (cellvar)
                    visitor(cellvar);
            }
            for (StrObject *freevar : code->freevars())
            {
                if (freevar)
                    visitor(freevar);
            }
        }

        Object *code_lines_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "code.co_lines() takes no arguments");
            CodeObject *code = require_code_instance(runtime, self, "co_lines");
            std::vector<Object *> entries;
            entries.reserve(code->instructions().size());
            std::vector<Rooted<Object>> roots;
            roots.reserve(code->instructions().size() * 4);
            for (std::size_t index = 0; index < code->instructions().size(); ++index)
            {
                Object *start = runtime.new_int(static_cast<std::int64_t>(index));
                Object *end = runtime.new_int(static_cast<std::int64_t>(index + 1));
                Object *line = runtime.new_int(static_cast<std::int64_t>(code->line_for_instruction(index)));
                roots.push_back(runtime.root(start));
                roots.push_back(runtime.root(end));
                roots.push_back(runtime.root(line));
                Object *entry = runtime.new_tuple({start, end, line});
                roots.push_back(runtime.root(entry));
                entries.push_back(entry);
            }
            Object *lines = runtime.new_list(std::move(entries));
            auto lines_root = runtime.root(lines);
            return object_iter(lines_root.get());
        }

        Object *code_cxx_instructions_method(Runtime &runtime, Object *self, ObjectSpan args)
        {
            if (!args.empty())
                raise_type_error(&runtime, "code._cxx_instructions() takes no arguments");
            CodeObject *code = require_code_instance(runtime, self, "_cxx_instructions");
            std::vector<Object *> entries;
            entries.reserve(code->instructions().size());
            std::vector<Rooted<Object>> roots;
            roots.reserve(code->instructions().size() * 7);
            std::uint32_t previous_line = 0;
            for (std::size_t index = 0; index < code->instructions().size(); ++index)
            {
                const Instruction instruction = code->instructions()[index];
                const std::uint32_t line = code->line_for_instruction(index);
                Object *offset = runtime.new_int(static_cast<std::int64_t>(index));
                Object *opname = runtime.new_str(opcode_name(instruction.opcode));
                Object *opcode = runtime.new_int(static_cast<std::int64_t>(instruction.opcode));
                Object *arg = runtime.new_int(static_cast<std::int64_t>(instruction.operand));
                Object *starts_line =
                    (line != 0 && line != previous_line)
                        ? static_cast<Object *>(runtime.new_int(static_cast<std::int64_t>(line)))
                        : static_cast<Object *>(runtime.none());
                Object *effect = runtime.new_int(static_cast<std::int64_t>(stack_effect(instruction)));
                roots.push_back(runtime.root(offset));
                roots.push_back(runtime.root(opname));
                roots.push_back(runtime.root(opcode));
                roots.push_back(runtime.root(arg));
                roots.push_back(runtime.root(starts_line));
                roots.push_back(runtime.root(effect));
                Object *entry = runtime.new_tuple({offset, opname, opcode, arg, starts_line, effect});
                roots.push_back(runtime.root(entry));
                entries.push_back(entry);
                if (line != 0)
                    previous_line = line;
            }
            Object *instructions = runtime.new_list(std::move(entries));
            auto instructions_root = runtime.root(instructions);
            return object_iter(instructions_root.get());
        }
    }

    void initialize_code_type_descriptors(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("co_name"),
                        runtime.new_getset_descriptor("co_name",
                                                      type,
                                                      code_name_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_filename"),
                        runtime.new_getset_descriptor("co_filename",
                                                      type,
                                                      code_filename_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_argcount"),
                        runtime.new_getset_descriptor("co_argcount",
                                                      type,
                                                      code_argcount_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_posonlyargcount"),
                        runtime.new_getset_descriptor("co_posonlyargcount",
                                                      type,
                                                      code_posonlyargcount_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_kwonlyargcount"),
                        runtime.new_getset_descriptor("co_kwonlyargcount",
                                                      type,
                                                      code_kwonlyargcount_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_flags"),
                        runtime.new_getset_descriptor("co_flags",
                                                      type,
                                                      code_flags_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_stacksize"),
                        runtime.new_getset_descriptor("co_stacksize",
                                                      type,
                                                      code_stacksize_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_consts"),
                        runtime.new_getset_descriptor("co_consts",
                                                      type,
                                                      code_consts_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_names"),
                        runtime.new_getset_descriptor("co_names",
                                                      type,
                                                      code_names_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_varnames"),
                        runtime.new_getset_descriptor("co_varnames",
                                                      type,
                                                      code_varnames_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_cellvars"),
                        runtime.new_getset_descriptor("co_cellvars",
                                                      type,
                                                      code_cellvars_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_freevars"),
                        runtime.new_getset_descriptor("co_freevars",
                                                      type,
                                                      code_freevars_getter,
                                                      nullptr));
        type->dict->set(runtime.new_str("co_firstlineno"),
                        runtime.new_getset_descriptor("co_firstlineno",
                                                      type,
                                                      code_firstlineno_getter,
                                                      nullptr));
    }

    void initialize_code_type_methods(Runtime &runtime, TypeObject *type)
    {
        type->dict->set(runtime.new_str("co_lines"),
                        runtime.new_method_descriptor("co_lines",
                                                      type,
                                                      code_lines_method,
                                                      "Return an iterator over bytecode offset and source line ranges.",
                                                      "($self, /)"));
        type->dict->set(runtime.new_str("_cxx_instructions"),
                        runtime.new_method_descriptor("_cxx_instructions",
                                                      type,
                                                      code_cxx_instructions_method,
                                                      "Return cxxpython-private instruction records for disassembly.",
                                                      "($self, /)"));
    }

    CodeObject::CodeObject(TypeObject *type,
                           std::string name,
                           std::string filename,
                           std::uint32_t arg_count,
                           std::uint32_t posonly_arg_count,
                           std::uint32_t kwonly_arg_count,
                           CodeObject::Flags flags,
                           std::vector<StrObject *> varnames,
                           std::vector<Object *> constants,
                           std::vector<StrObject *> names,
                           std::vector<StrObject *> cellvars,
                           std::vector<StrObject *> freevars,
                           std::vector<Instruction> instructions,
                           std::vector<std::uint32_t> lines)
        : Object(type),
          name_(std::move(name)),
          filename_(std::move(filename)),
          arg_count_(arg_count),
          posonly_arg_count_(posonly_arg_count),
          kwonly_arg_count_(kwonly_arg_count),
          flags_(flags),
          varnames_(std::move(varnames)),
          constants_(std::move(constants)),
          names_(std::move(names)),
          cellvars_(std::move(cellvars)),
          freevars_(std::move(freevars)),
          instructions_(std::move(instructions)),
          lines_(std::move(lines))
    {
        if (posonly_arg_count_ > arg_count_)
            throw BytecodeError("code object positional-only count exceeds argument count");
        if (!lines_.empty() && lines_.size() != instructions_.size())
            throw BytecodeError("code object line table must match instruction count");
        struct State
        {
            std::size_t index;
            int depth;
        };

        std::vector<std::vector<int>> seen_depths(instructions_.size());
        std::queue<State> worklist;
        worklist.push(State{0, 0});
        int max_depth = 0;

        auto push_state = [&](std::size_t successor, int depth) {
            if (depth < 0)
                throw BytecodeError("code object stack effect underflow");
            if (successor >= instructions_.size())
                return;
            std::vector<int> &depths = seen_depths[successor];
            if (std::find(depths.begin(), depths.end(), depth) != depths.end())
                return;
            depths.push_back(depth);
            worklist.push(State{successor, depth});
        };

        while (!worklist.empty())
        {
            const State state = worklist.front();
            worklist.pop();
            const Instruction instruction = instructions_[state.index];
            const int entry_depth = state.depth;

            if (entry_depth > max_depth)
                max_depth = entry_depth;

            auto apply = [&](std::size_t successor, int delta) {
                push_state(successor, entry_depth + delta);
            };

            switch (instruction.opcode)
            {
            case Opcode::ReturnValue:
            case Opcode::RaiseValue:
                apply(instructions_.size(), stack_effect(instruction));
                break;
            case Opcode::JumpAbsolute:
            case Opcode::BreakLoop:
            case Opcode::ContinueLoop:
                apply(instruction.operand, 0);
                break;
            case Opcode::PopJumpIfFalse:
            case Opcode::PopJumpIfTrue:
                apply(state.index + 1, -1);
                apply(instruction.operand, -1);
                break;
            case Opcode::JumpIfFalseOrPop:
                apply(state.index + 1, -1);
                apply(instruction.operand, 0);
                break;
            case Opcode::ForIter:
                apply(state.index + 1, 1);
                apply(instruction.operand, -1);
                break;
            default:
                apply(state.index + 1, stack_effect(instruction));
                break;
            }
        }
        stack_size_ = static_cast<std::uint32_t>(max_depth);
    }

    const std::string &CodeObject::name() const noexcept
    {
        return name_;
    }

    const std::string &CodeObject::filename() const noexcept
    {
        return filename_;
    }

    std::uint32_t CodeObject::arg_count() const noexcept
    {
        return arg_count_;
    }

    std::uint32_t CodeObject::posonly_arg_count() const noexcept
    {
        return posonly_arg_count_;
    }

    std::uint32_t CodeObject::kwonly_arg_count() const noexcept
    {
        return kwonly_arg_count_;
    }

    CodeObject::Flags CodeObject::flags() const noexcept
    {
        return flags_;
    }

    bool CodeObject::has_varargs() const noexcept
    {
        return (flags_ & FlagVarArgs) != 0;
    }

    bool CodeObject::has_varkeywords() const noexcept
    {
        return (flags_ & FlagVarKeywords) != 0;
    }

    std::uint32_t CodeObject::varargs_index() const noexcept
    {
        return arg_count_ + kwonly_arg_count_;
    }

    std::uint32_t CodeObject::varkeywords_index() const noexcept
    {
        return arg_count_ + kwonly_arg_count_ + (has_varargs() ? 1U : 0U);
    }

    const std::vector<StrObject *> &CodeObject::varnames() const noexcept
    {
        return varnames_;
    }

    const std::vector<Object *> &CodeObject::constants() const noexcept
    {
        return constants_;
    }

    const std::vector<StrObject *> &CodeObject::names() const noexcept
    {
        return names_;
    }

    const std::vector<StrObject *> &CodeObject::cellvars() const noexcept
    {
        return cellvars_;
    }

    const std::vector<StrObject *> &CodeObject::freevars() const noexcept
    {
        return freevars_;
    }

    const std::vector<Instruction> &CodeObject::instructions() const noexcept
    {
        return instructions_;
    }

    const std::vector<std::uint32_t> &CodeObject::lines() const noexcept
    {
        return lines_;
    }

    std::uint32_t CodeObject::stack_size() const noexcept
    {
        return stack_size_;
    }

    std::uint32_t CodeObject::line_for_instruction(std::size_t instruction_index) const noexcept
    {
        if (instruction_index >= lines_.size())
            return 0;
        return lines_[instruction_index];
    }

    TypeSlots make_code_slots()
    {
        return TypeSlots{
            .repr = code_repr,
            .hash = code_hash,
            .equal = code_equal,
            .truth = code_truth,
            .traverse = code_traverse,
        };
    }

    const char *opcode_name(Opcode opcode)
    {
        switch (opcode)
        {
        case Opcode::LoadConst:
            return "LOAD_CONST";
        case Opcode::LoadName:
            return "LOAD_NAME";
        case Opcode::StoreName:
            return "STORE_NAME";
        case Opcode::DeleteName:
            return "DELETE_NAME";
        case Opcode::LoadGlobal:
            return "LOAD_GLOBAL";
        case Opcode::StoreGlobal:
            return "STORE_GLOBAL";
        case Opcode::DeleteGlobal:
            return "DELETE_GLOBAL";
        case Opcode::LoadFast:
            return "LOAD_FAST";
        case Opcode::StoreFast:
            return "STORE_FAST";
        case Opcode::DeleteFast:
            return "DELETE_FAST";
        case Opcode::LoadDeref:
            return "LOAD_DEREF";
        case Opcode::StoreDeref:
            return "STORE_DEREF";
        case Opcode::DeleteDeref:
            return "DELETE_DEREF";
        case Opcode::LoadClosure:
            return "LOAD_CLOSURE";
        case Opcode::LoadAttr:
            return "LOAD_ATTR";
        case Opcode::StoreAttr:
            return "STORE_ATTR";
        case Opcode::DeleteAttr:
            return "DELETE_ATTR";
        case Opcode::PopTop:
            return "POP_TOP";
        case Opcode::DupTop:
            return "DUP_TOP";
        case Opcode::DupTwo:
            return "DUP_TWO";
        case Opcode::RotTwo:
            return "ROT_TWO";
        case Opcode::RotThree:
            return "ROT_THREE";
        case Opcode::UnaryOp:
            return "UNARY_OP";
        case Opcode::BinaryOp:
            return "BINARY_OP";
        case Opcode::InplaceBinaryOp:
            return "INPLACE_BINARY_OP";
        case Opcode::FormatValue:
            return "FORMAT_VALUE";
        case Opcode::BinarySubscr:
            return "BINARY_SUBSCR";
        case Opcode::StoreSubscr:
            return "STORE_SUBSCR";
        case Opcode::DeleteSubscr:
            return "DELETE_SUBSCR";
        case Opcode::BuildList:
            return "BUILD_LIST";
        case Opcode::BuildTuple:
            return "BUILD_TUPLE";
        case Opcode::BuildTupleUnpack:
            return "BUILD_TUPLE_UNPACK";
        case Opcode::BuildMap:
            return "BUILD_MAP";
        case Opcode::BuildKwMapUnpack:
            return "BUILD_KW_MAP_UNPACK";
        case Opcode::BuildSlice:
            return "BUILD_SLICE";
        case Opcode::CompareOp:
            return "COMPARE_OP";
        case Opcode::GetIter:
            return "GET_ITER";
        case Opcode::ForIter:
            return "FOR_ITER";
        case Opcode::BreakLoop:
            return "BREAK_LOOP";
        case Opcode::ContinueLoop:
            return "CONTINUE_LOOP";
        case Opcode::JumpAbsolute:
            return "JUMP_ABSOLUTE";
        case Opcode::PopJumpIfFalse:
            return "POP_JUMP_IF_FALSE";
        case Opcode::PopJumpIfTrue:
            return "POP_JUMP_IF_TRUE";
        case Opcode::JumpIfFalseOrPop:
            return "JUMP_IF_FALSE_OR_POP";
        case Opcode::MakeFunction:
            return "MAKE_FUNCTION";
        case Opcode::MakeClass:
            return "MAKE_CLASS";
        case Opcode::Call:
            return "CALL";
        case Opcode::CallKw:
            return "CALL_KW";
        case Opcode::CallEx:
            return "CALL_EX";
        case Opcode::ImportStar:
            return "IMPORT_STAR";
        case Opcode::SetupExcept:
            return "SETUP_EXCEPT";
        case Opcode::SetupFinally:
            return "SETUP_FINALLY";
        case Opcode::PopExcept:
            return "POP_EXCEPT";
        case Opcode::PopFinally:
            return "POP_FINALLY";
        case Opcode::CheckExceptionMatch:
            return "CHECK_EXCEPTION_MATCH";
        case Opcode::RaiseValue:
            return "RAISE_VALUE";
        case Opcode::WithExit:
            return "WITH_EXIT";
        case Opcode::EndFinally:
            return "END_FINALLY";
        case Opcode::ReturnValue:
            return "RETURN_VALUE";
        }
        return "<invalid opcode>";
    }

    int stack_effect(Instruction instruction)
    {
        switch (instruction.opcode)
        {
        case Opcode::LoadConst:
        case Opcode::LoadName:
        case Opcode::LoadGlobal:
        case Opcode::LoadFast:
        case Opcode::LoadDeref:
        case Opcode::LoadClosure:
            return 1;
        case Opcode::LoadAttr:
        case Opcode::DeleteName:
        case Opcode::DeleteGlobal:
        case Opcode::DeleteFast:
        case Opcode::DeleteDeref:
        case Opcode::JumpAbsolute:
        case Opcode::BreakLoop:
        case Opcode::ContinueLoop:
        case Opcode::SetupExcept:
        case Opcode::SetupFinally:
        case Opcode::PopExcept:
        case Opcode::PopFinally:
        case Opcode::CheckExceptionMatch:
        case Opcode::EndFinally:
        case Opcode::RotTwo:
        case Opcode::RotThree:
        case Opcode::UnaryOp:
        case Opcode::GetIter:
        case Opcode::FormatValue:
            return 0;
        case Opcode::ForIter:
            return 1;
        case Opcode::MakeFunction:
        {
            const std::uint32_t closure_count =
                instruction.operand & MakeFunctionClosureCountMask;
            const bool has_defaults =
                (instruction.operand & MakeFunctionHasDefaults) != 0;
            const bool has_kwdefaults =
                (instruction.operand & MakeFunctionHasKwDefaults) != 0;
            const bool has_annotations =
                (instruction.operand & MakeFunctionHasAnnotations) != 0;
            return 1 - 1 - static_cast<int>(closure_count != 0) -
                   static_cast<int>(has_defaults) - static_cast<int>(has_kwdefaults) -
                   static_cast<int>(has_annotations);
        }
        case Opcode::MakeClass:
            return -2;
        case Opcode::DupTop:
            return 1;
        case Opcode::DupTwo:
            return 2;
        case Opcode::StoreName:
        case Opcode::StoreGlobal:
        case Opcode::StoreFast:
        case Opcode::StoreDeref:
        case Opcode::PopTop:
        case Opcode::ImportStar:
        case Opcode::DeleteAttr:
        case Opcode::PopJumpIfFalse:
        case Opcode::PopJumpIfTrue:
        case Opcode::JumpIfFalseOrPop:
            return -1;
        case Opcode::StoreAttr:
            return -2;
        case Opcode::BinaryOp:
        case Opcode::InplaceBinaryOp:
        case Opcode::BinarySubscr:
        case Opcode::CompareOp:
            return -1;
        case Opcode::StoreSubscr:
            return -3;
        case Opcode::DeleteSubscr:
            return -2;
        case Opcode::BuildList:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildTuple:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildTupleUnpack:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildMap:
            return 1 - 2 * static_cast<int>(instruction.operand);
        case Opcode::BuildKwMapUnpack:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::BuildSlice:
            return 1 - static_cast<int>(instruction.operand);
        case Opcode::Call:
            return -static_cast<int>(instruction.operand);
        case Opcode::CallKw:
            return -static_cast<int>(instruction.operand) - 1;
        case Opcode::CallEx:
            return -2;
        case Opcode::WithExit:
            return -1;
        case Opcode::ReturnValue:
        case Opcode::RaiseValue:
            return -1;
        }
        throw BytecodeError("invalid opcode for stack effect");
    }
}
