#include "cxxpython/compiler.hpp"

#include "cxxpython/compiler_internal.hpp"
#include "compiler_symbols.hpp"

#include <string_view>
#include <utility>

namespace cxxpython
{
    namespace
    {
        class BytecodeBuilder final : public compiler_internal::ExprVisitor,
                                      public compiler_internal::TargetVisitor,
                                      public compiler_internal::StmtVisitor
        {
        public:
            BytecodeBuilder(Runtime &runtime,
                            const SymbolTable &symbols,
                            std::string filename,
                            std::uint32_t initial_line = 1)
                : runtime_(runtime),
                  symbols_(symbols),
                  filename_(std::move(filename)),
                  names_(symbols.make_name_objects(runtime)),
                  varnames_(symbols.make_varname_objects(runtime)),
                  cellvars_(symbols.make_cellvar_objects(runtime)),
                  freevars_(symbols.make_freevar_objects(runtime)),
                  current_line_(initial_line)
            {
            }

            CodeObject *build_eval(const compiler_internal::Expr &expression)
            {
                expression.accept(*this);
                emit({Opcode::ReturnValue, 0});
                return runtime_.new_code("<eval>",
                                         filename_,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_),
                                         std::move(lines_));
            }

            CodeObject *build_exec(const compiler_internal::Module &module)
            {
                for (const auto &statement : module.statements)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                emit({Opcode::ReturnValue, 0});
                return runtime_.new_code("<module>",
                                         filename_,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_),
                                         std::move(lines_));
            }

            CodeObject *build_function(const compiler_internal::FunctionDefStmt &function)
            {
                for (const auto &statement : function.body)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                emit({Opcode::ReturnValue, 0});
                return runtime_.new_code(function.name,
                                         filename_,
                                         static_cast<std::uint32_t>(function.params.positional.size()),
                                         function_flags(function.params),
                                         std::move(varnames_),
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(cellvars_),
                                         std::move(freevars_),
                                         std::move(instructions_),
                                         std::move(lines_));
            }

            CodeObject *build_class(const compiler_internal::ClassDefStmt &klass)
            {
                for (const auto &statement : klass.body)
                    statement->accept(*this);
                if (symbols_.is_cellvar("__class__"))
                {
                    emit({
                        Opcode::LoadClosure,
                        static_cast<std::uint32_t>(deref_index("__class__")),
                    });
                    emit({
                        Opcode::StoreName,
                        static_cast<std::uint32_t>(name_index("__classcell__")),
                    });
                }
                emit_constant(runtime_.none());
                emit({Opcode::ReturnValue, 0});
                return runtime_.new_code(klass.name,
                                         filename_,
                                         0,
                                         0,
                                         {},
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(cellvars_),
                                         std::move(freevars_),
                                         std::move(instructions_),
                                         std::move(lines_));
            }

            void visit(const compiler_internal::IntegerExpr &expression) override
            {
                emit_constant(runtime_.new_int(expression.value));
            }

            void visit(const compiler_internal::StringExpr &expression) override
            {
                emit_constant(runtime_.new_str(expression.value));
            }

            void visit(const compiler_internal::FStringExpr &expression) override
            {
                std::size_t emitted_parts = 0;
                for (const compiler_internal::FStringPart &part : expression.parts)
                {
                    if (part.is_expression())
                    {
                        part.expression->accept(*this);
                        if (part.conversion == compiler_internal::FStringConversion::Repr)
                        {
                            emit({
                                Opcode::LoadName,
                                static_cast<std::uint32_t>(ensure_name_index("repr")),
                            });
                            emit({Opcode::RotTwo, 0});
                            emit({Opcode::Call, 1});
                        }
                        else if (part.conversion == compiler_internal::FStringConversion::Str)
                        {
                            emit({
                                Opcode::LoadName,
                                static_cast<std::uint32_t>(ensure_name_index("str")),
                            });
                            emit({Opcode::RotTwo, 0});
                            emit({Opcode::Call, 1});
                        }
                        if (part.format_spec.empty())
                        {
                            emit({Opcode::FormatValue, 0});
                        }
                        else
                        {
                            emit({
                                Opcode::LoadName,
                                static_cast<std::uint32_t>(ensure_name_index("format")),
                            });
                            emit({Opcode::RotTwo, 0});
                            emit_constant(runtime_.new_str(part.format_spec));
                            emit({Opcode::Call, 2});
                        }
                    }
                    else
                    {
                        if (part.literal.empty())
                            continue;
                        emit_constant(runtime_.new_str(part.literal));
                    }
                    if (emitted_parts != 0)
                    {
                        emit({
                            Opcode::BinaryOp,
                            static_cast<std::uint32_t>(BinaryOp::Add),
                        });
                    }
                    ++emitted_parts;
                }
                if (emitted_parts == 0)
                    emit_constant(runtime_.new_str(""));
            }

            void visit(const compiler_internal::UnaryExpr &expression) override
            {
                expression.operand->accept(*this);
                emit({
                    Opcode::UnaryOp,
                    static_cast<std::uint32_t>(expression.op),
                });
            }

            void visit(const compiler_internal::BinaryExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
                emit({
                    Opcode::BinaryOp,
                    static_cast<std::uint32_t>(expression.op),
                });
            }

            void visit(const compiler_internal::CompareExpr &expression) override
            {
                expression.left->accept(*this);
                std::vector<std::size_t> cleanup_jumps;
                for (std::size_t index = 0; index < expression.comparisons.size(); ++index)
                {
                    const compiler_internal::Comparison &comparison = expression.comparisons[index];
                    const bool last = index + 1 == expression.comparisons.size();
                    comparison.right->accept(*this);
                    if (!last)
                    {
                        emit({Opcode::DupTop, 0});
                        emit({Opcode::RotThree, 0});
                    }
                    emit({
                        Opcode::CompareOp,
                        static_cast<std::uint32_t>(comparison.op),
                    });
                    if (!last)
                        cleanup_jumps.push_back(emit_jump(Opcode::JumpIfFalseOrPop));
                }

                if (!cleanup_jumps.empty())
                {
                    const std::size_t end_jump = emit_jump(Opcode::JumpAbsolute);
                    const std::size_t cleanup_target = instructions_.size();
                    emit({Opcode::RotTwo, 0});
                    emit({Opcode::PopTop, 0});
                    for (std::size_t jump : cleanup_jumps)
                        patch_jump(jump, cleanup_target);
                    patch_jump(end_jump, instructions_.size());
                }
            }

            void visit(const compiler_internal::ConditionalExpr &expression) override
            {
                expression.condition->accept(*this);
                const std::size_t false_jump = emit_jump(Opcode::PopJumpIfFalse);
                expression.body->accept(*this);
                const std::size_t end_jump = emit_jump(Opcode::JumpAbsolute);
                patch_jump(false_jump, instructions_.size());
                expression.else_body->accept(*this);
                patch_jump(end_jump, instructions_.size());
            }

            void visit(const compiler_internal::SingletonExpr &expression) override
            {
                switch (expression.kind)
                {
                case compiler_internal::SingletonKind::TrueValue:
                    emit_constant(runtime_.true_value());
                    return;
                case compiler_internal::SingletonKind::FalseValue:
                    emit_constant(runtime_.false_value());
                    return;
                case compiler_internal::SingletonKind::NoneValue:
                    emit_constant(runtime_.none());
                    return;
                }
                raise_syntax_error(&runtime_, "unknown singleton literal");
            }

            void visit(const compiler_internal::NameExpr &expression) override
            {
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_global(expression.value))
                {
                    emit({
                        Opcode::LoadGlobal,
                        static_cast<std::uint32_t>(name_index(expression.value)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_cellvar(expression.value))
                {
                    emit({
                        Opcode::LoadDeref,
                        static_cast<std::uint32_t>(deref_index(expression.value)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_freevar(expression.value))
                {
                    emit({
                        Opcode::LoadDeref,
                        static_cast<std::uint32_t>(deref_index(expression.value)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(expression.value))
                {
                    emit({
                        Opcode::LoadFast,
                        static_cast<std::uint32_t>(local_index(expression.value)),
                    });
                    return;
                }
                emit({
                    Opcode::LoadName,
                    static_cast<std::uint32_t>(name_index(expression.value)),
                });
            }

            void visit(const compiler_internal::AttributeExpr &expression) override
            {
                expression.object->accept(*this);
                emit({
                    Opcode::LoadAttr,
                    static_cast<std::uint32_t>(name_index(expression.name)),
                });
            }

            void visit(const compiler_internal::SubscriptExpr &expression) override
            {
                expression.object->accept(*this);
                expression.key->accept(*this);
                emit({Opcode::BinarySubscr, 0});
            }

            void visit(const compiler_internal::SliceExpr &expression) override
            {
                if (expression.start)
                    expression.start->accept(*this);
                else
                    emit_constant(runtime_.none());
                if (expression.stop)
                    expression.stop->accept(*this);
                else
                    emit_constant(runtime_.none());
                if (expression.step)
                    expression.step->accept(*this);
                else
                    emit_constant(runtime_.none());
                emit({Opcode::BuildSlice, 3});
            }

            void visit(const compiler_internal::ListExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
                emit({
                    Opcode::BuildList,
                    static_cast<std::uint32_t>(expression.items.size()),
                });
            }

            void visit(const compiler_internal::TupleExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
                emit({
                    Opcode::BuildTuple,
                    static_cast<std::uint32_t>(expression.items.size()),
                });
            }

            void visit(const compiler_internal::DictExpr &expression) override
            {
                for (const compiler_internal::DictItem &item : expression.items)
                {
                    item.key->accept(*this);
                    item.value->accept(*this);
                }
                emit({
                    Opcode::BuildMap,
                    static_cast<std::uint32_t>(expression.items.size()),
                });
            }

            void visit(const compiler_internal::CallExpr &expression) override
            {
                expression.callee->accept(*this);
                std::size_t positional_count = 0;
                std::size_t keyword_count = 0;
                for (const auto &arg : expression.args)
                {
                    if (arg.kind == compiler_internal::CallArgumentKind::Positional)
                    {
                        arg.value->accept(*this);
                        ++positional_count;
                    }
                    else
                    {
                        emit_constant(runtime_.new_str(arg.keyword));
                        arg.value->accept(*this);
                        ++keyword_count;
                    }
                }
                if (keyword_count != 0)
                {
                    emit({
                        Opcode::BuildMap,
                        static_cast<std::uint32_t>(keyword_count),
                    });
                    emit({
                        Opcode::CallKw,
                        static_cast<std::uint32_t>(positional_count),
                    });
                    return;
                }
                emit({
                    Opcode::Call,
                    static_cast<std::uint32_t>(positional_count),
                });
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                if (deleting_target_)
                {
                    if (symbols_.scope() == SymbolScope::Function && symbols_.is_global(target.name))
                    {
                        emit({
                            Opcode::DeleteGlobal,
                            static_cast<std::uint32_t>(name_index(target.name)),
                        });
                        return;
                    }
                    if (symbols_.scope() == SymbolScope::Function && symbols_.is_cellvar(target.name))
                    {
                        emit({
                            Opcode::DeleteDeref,
                            static_cast<std::uint32_t>(deref_index(target.name)),
                        });
                        return;
                    }
                    if (symbols_.scope() == SymbolScope::Function && symbols_.is_freevar(target.name))
                    {
                        emit({
                            Opcode::DeleteDeref,
                            static_cast<std::uint32_t>(deref_index(target.name)),
                        });
                        return;
                    }
                    if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(target.name))
                    {
                        emit({
                            Opcode::DeleteFast,
                            static_cast<std::uint32_t>(local_index(target.name)),
                        });
                        return;
                    }
                    emit({
                        Opcode::DeleteName,
                        static_cast<std::uint32_t>(name_index(target.name)),
                    });
                    return;
                }
                emit_pending_assignment_value();
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_global(target.name))
                {
                    emit({
                        Opcode::StoreGlobal,
                        static_cast<std::uint32_t>(name_index(target.name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_cellvar(target.name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(target.name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_freevar(target.name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(target.name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(target.name))
                {
                    emit({
                        Opcode::StoreFast,
                        static_cast<std::uint32_t>(local_index(target.name)),
                    });
                    return;
                }
                emit({
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(target.name)),
                });
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                target.object->accept(*this);
                if (deleting_target_)
                {
                    emit({
                        Opcode::DeleteAttr,
                        static_cast<std::uint32_t>(name_index(target.name)),
                    });
                    return;
                }
                emit_pending_assignment_value();
                emit({
                    Opcode::StoreAttr,
                    static_cast<std::uint32_t>(name_index(target.name)),
                });
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                target.object->accept(*this);
                target.key->accept(*this);
                if (deleting_target_)
                {
                    emit({Opcode::DeleteSubscr, 0});
                    return;
                }
                emit_pending_assignment_value();
                emit({Opcode::StoreSubscr, 0});
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                statement.expression->accept(*this);
                emit({Opcode::PopTop, 0});
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                PendingAssignmentValue pending(*this, *statement.value);
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::DeleteStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                DeletingTarget deleting(*this);
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                statement.condition->accept(*this);
                const std::size_t false_jump = emit_jump(Opcode::PopJumpIfFalse);
                for (const auto &item : statement.then_body)
                    item->accept(*this);

                if (statement.else_body.empty())
                {
                    patch_jump(false_jump, instructions_.size());
                    return;
                }

                const std::size_t end_jump = emit_jump(Opcode::JumpAbsolute);
                patch_jump(false_jump, instructions_.size());
                for (const auto &item : statement.else_body)
                    item->accept(*this);
                patch_jump(end_jump, instructions_.size());
            }

            void visit(const compiler_internal::ForStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                statement.iterable->accept(*this);
                emit({Opcode::GetIter, 0});
                const std::size_t loop_start = instructions_.size();
                const std::size_t exit_jump = emit_jump(Opcode::ForIter);
                loop_stack_.push_back(LoopControl{
                    .start = loop_start,
                    .continue_target = loop_start,
                    .break_target = 0,
                    .break_jumps = {},
                    .continue_jumps = {},
                });
                emit_binding_store(statement.target);
                for (const auto &item : statement.body)
                    item->accept(*this);
                emit({
                    Opcode::JumpAbsolute,
                    static_cast<std::uint32_t>(loop_start),
                });
                const std::size_t else_start = instructions_.size();
                patch_jump(exit_jump, else_start);
                LoopControl loop = std::move(loop_stack_.back());
                loop_stack_.pop_back();
                for (std::size_t jump : loop.continue_jumps)
                    patch_jump(jump, loop.continue_target);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
                const std::size_t loop_end = instructions_.size();
                loop.break_target = loop_end;
                for (std::size_t jump : loop.break_jumps)
                    patch_jump(jump, loop.break_target);
            }

            void visit(const compiler_internal::PassStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
            }

            void visit(const compiler_internal::BreakStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                if (loop_stack_.empty())
                    raise_syntax_error(&runtime_, "'break' outside loop");
                loop_stack_.back().break_jumps.push_back(emit_jump(Opcode::BreakLoop));
            }

            void visit(const compiler_internal::ContinueStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                if (loop_stack_.empty())
                    raise_syntax_error(&runtime_, "'continue' outside loop");
                loop_stack_.back().continue_jumps.push_back(emit_jump(Opcode::ContinueLoop));
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                if (statement.value)
                    statement.value->accept(*this);
                else
                    emit_constant(runtime_.none());
                emit({Opcode::ReturnValue, 0});
            }

            void visit(const compiler_internal::RaiseStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                statement.value->accept(*this);
                emit({Opcode::RaiseValue, 0});
            }

            void visit(const compiler_internal::GlobalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::NonlocalStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::ImportStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                {
                    emit_import_call(alias.name, runtime_.new_tuple({}));
                    if (alias.bind_full_module)
                        emit_load_dotted_tail(alias.name);
                    emit_binding_store(alias.binding);
                }
            }

            void visit(const compiler_internal::FromImportStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                std::vector<Object *> fromlist;
                fromlist.reserve(statement.aliases.size());
                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                    fromlist.push_back(runtime_.new_str(alias.name));
                emit_import_call(statement.module, runtime_.new_tuple(std::move(fromlist)));

                for (const compiler_internal::ImportAlias &alias : statement.aliases)
                {
                    emit({Opcode::DupTop, 0});
                    emit({
                        Opcode::LoadAttr,
                        static_cast<std::uint32_t>(name_index(alias.name)),
                    });
                    emit_binding_store(alias.binding);
                }
                emit({Opcode::PopTop, 0});
            }

            void visit(const compiler_internal::TryStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                const bool has_except = !statement.handlers.empty();
                const bool has_finally = !statement.finally_body.empty();
                const std::size_t setup_finally = has_finally ? emit_jump(Opcode::SetupFinally) : 0;
                const std::size_t setup_except = has_except ? emit_jump(Opcode::SetupExcept) : 0;

                for (const auto &item : statement.body)
                    item->accept(*this);

                if (has_except)
                    emit({Opcode::PopExcept, 0});
                if (has_finally)
                    emit({Opcode::PopFinally, 0});
                const std::size_t exit_jump = emit_jump(Opcode::JumpAbsolute);

                std::vector<std::size_t> handler_exit_jumps;
                if (has_except)
                {
                    const std::size_t handlers_start = instructions_.size();
                    patch_jump(setup_except, handlers_start);
                    std::vector<std::size_t> next_handler_jumps;
                    for (const compiler_internal::ExceptHandler &handler : statement.handlers)
                    {
                        for (std::size_t jump : next_handler_jumps)
                            patch_jump(jump, instructions_.size());
                        next_handler_jumps.clear();

                        if (handler.type)
                        {
                            handler.type->accept(*this);
                            emit({Opcode::CheckExceptionMatch, 0});
                            next_handler_jumps.push_back(emit_jump(Opcode::PopJumpIfFalse));
                        }

                        if (handler.name)
                        {
                            emit({Opcode::DupTop, 0});
                            emit_store_name(*handler.name);
                        }
                        emit({Opcode::PopTop, 0});
                        for (const auto &item : handler.body)
                            item->accept(*this);
                        if (has_finally)
                            emit({Opcode::PopFinally, 0});
                        handler_exit_jumps.push_back(emit_jump(Opcode::JumpAbsolute));
                    }
                    for (std::size_t jump : next_handler_jumps)
                        patch_jump(jump, instructions_.size());
                    emit({Opcode::RaiseValue, 0});
                }

                const std::size_t finally_start = instructions_.size();
                if (has_finally)
                {
                    patch_jump(setup_finally, finally_start);
                    patch_jump(exit_jump, finally_start);
                    for (std::size_t jump : handler_exit_jumps)
                        patch_jump(jump, finally_start);
                    for (const auto &item : statement.finally_body)
                        item->accept(*this);
                    emit({Opcode::EndFinally, 0});
                }
                else
                {
                    patch_jump(exit_jump, finally_start);
                    for (std::size_t jump : handler_exit_jumps)
                        patch_jump(jump, finally_start);
                }
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                emit_decorators(statement.decorators);
                SymbolTable function_symbols = collect_function_symbols(statement);
                for (const std::string &freevar : function_symbols.freevars())
                {
                    emit({
                        Opcode::LoadClosure,
                        static_cast<std::uint32_t>(deref_index(freevar)),
                    });
                }
                if (!function_symbols.freevars().empty())
                {
                    emit({
                        Opcode::BuildTuple,
                        static_cast<std::uint32_t>(function_symbols.freevars().size()),
                    });
                }
                BytecodeBuilder function_builder(
                    runtime_,
                    function_symbols,
                    filename_,
                    static_cast<std::uint32_t>(statement.line));
                CodeObject *code = function_builder.build_function(statement);
                emit_constant(code);
                emit({
                    Opcode::MakeFunction,
                    static_cast<std::uint32_t>(function_symbols.freevars().size()),
                });
                apply_decorators(statement.decorators.size());
                emit_binding_store(statement.name);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                StatementLineGuard line(*this, statement);
                emit_decorators(statement.decorators);
                emit({Opcode::LoadName, static_cast<std::uint32_t>(name_index("__build_class__"))});
                SymbolTable class_symbols = collect_class_symbols(statement);
                BytecodeBuilder class_builder(
                    runtime_,
                    class_symbols,
                    filename_,
                    static_cast<std::uint32_t>(statement.line));
                CodeObject *code = class_builder.build_class(statement);
                emit_constant(code);
                emit({Opcode::MakeFunction, 0});
                emit_constant(runtime_.new_str(statement.name));
                for (const auto &base : statement.bases)
                    base->accept(*this);
                for (const auto &keyword : statement.keywords)
                {
                    emit_constant(runtime_.new_str(keyword.keyword));
                    keyword.value->accept(*this);
                }
                const std::uint32_t positional_count =
                    static_cast<std::uint32_t>(2 + statement.bases.size());
                if (!statement.keywords.empty())
                {
                    emit({
                        Opcode::BuildMap,
                        static_cast<std::uint32_t>(statement.keywords.size()),
                    });
                    emit({Opcode::CallKw, positional_count});
                }
                else
                {
                    emit({Opcode::Call, positional_count});
                }
                apply_decorators(statement.decorators.size());
                emit_binding_store(statement.name);
            }

        private:
            class StatementLineGuard final
            {
            public:
                StatementLineGuard(BytecodeBuilder &builder, const compiler_internal::Stmt &statement)
                    : builder_(builder), previous_(builder.current_line_)
                {
                    builder_.current_line_ = static_cast<std::uint32_t>(statement.line);
                }

                ~StatementLineGuard()
                {
                    builder_.current_line_ = previous_;
                }

                StatementLineGuard(const StatementLineGuard &) = delete;
                StatementLineGuard &operator=(const StatementLineGuard &) = delete;

            private:
                BytecodeBuilder &builder_;
                std::uint32_t previous_;
            };

            class PendingAssignmentValue final
            {
            public:
                PendingAssignmentValue(BytecodeBuilder &builder, const compiler_internal::Expr &value)
                    : builder_(builder), previous_(builder.pending_assignment_value_)
                {
                    builder_.pending_assignment_value_ = &value;
                }

                ~PendingAssignmentValue()
                {
                    builder_.pending_assignment_value_ = previous_;
                }

                PendingAssignmentValue(const PendingAssignmentValue &) = delete;
                PendingAssignmentValue &operator=(const PendingAssignmentValue &) = delete;

            private:
                BytecodeBuilder &builder_;
                const compiler_internal::Expr *previous_;
            };

            class DeletingTarget final
            {
            public:
                explicit DeletingTarget(BytecodeBuilder &builder)
                    : builder_(builder), previous_(builder.deleting_target_)
                {
                    builder_.deleting_target_ = true;
                }

                ~DeletingTarget()
                {
                    builder_.deleting_target_ = previous_;
                }

                DeletingTarget(const DeletingTarget &) = delete;
                DeletingTarget &operator=(const DeletingTarget &) = delete;

            private:
                BytecodeBuilder &builder_;
                bool previous_;
            };

            void emit_pending_assignment_value()
            {
                if (!pending_assignment_value_)
                    raise_syntax_error(&runtime_, "assignment target emitted without value");
                pending_assignment_value_->accept(*this);
            }

            void emit_import_call(std::string_view module_name, TupleObject *fromlist)
            {
                emit({
                    Opcode::LoadName,
                    static_cast<std::uint32_t>(name_index("__import__")),
                });
                emit_constant(runtime_.new_str(std::string(module_name)));
                emit_constant(runtime_.none());
                emit_constant(runtime_.none());
                emit_constant(fromlist);
                emit_constant(runtime_.new_int(0));
                emit({Opcode::Call, 5});
            }

            void emit_load_dotted_tail(std::string_view module_name)
            {
                std::size_t start = module_name.find('.');
                while (start != std::string_view::npos)
                {
                    const std::size_t component_start = start + 1;
                    const std::size_t next = module_name.find('.', component_start);
                    const std::size_t component_end =
                        next == std::string_view::npos ? module_name.size() : next;
                    emit({
                        Opcode::LoadAttr,
                        static_cast<std::uint32_t>(
                            ensure_name_index(std::string(module_name.substr(component_start,
                                                                            component_end - component_start)))),
                    });
                    start = next;
                }
            }

            void emit_constant(Object *constant)
            {
                constants_.push_back(constant);
                emit({
                    Opcode::LoadConst,
                    static_cast<std::uint32_t>(constants_.size() - 1),
                });
            }

            void emit_decorators(const std::vector<std::unique_ptr<compiler_internal::Expr>> &decorators)
            {
                for (const auto &decorator : decorators)
                    decorator->accept(*this);
            }

            void apply_decorators(std::size_t count)
            {
                for (std::size_t index = 0; index < count; ++index)
                {
                    emit({
                        Opcode::Call,
                        1,
                    });
                }
            }

            void emit(Instruction instruction)
            {
                instructions_.push_back(instruction);
                lines_.push_back(current_line_);
            }

            void emit_binding_store(const std::string &name)
            {
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_global(name))
                {
                    emit({
                        Opcode::StoreGlobal,
                        static_cast<std::uint32_t>(name_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_cellvar(name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_freevar(name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(name))
                {
                    emit({
                        Opcode::StoreFast,
                        static_cast<std::uint32_t>(local_index(name)),
                    });
                    return;
                }
                emit({
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(name)),
                });
            }

            void emit_store_name(const std::string &name)
            {
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_global(name))
                {
                    emit({
                        Opcode::StoreGlobal,
                        static_cast<std::uint32_t>(name_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_cellvar(name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_freevar(name))
                {
                    emit({
                        Opcode::StoreDeref,
                        static_cast<std::uint32_t>(deref_index(name)),
                    });
                    return;
                }
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(name))
                {
                    emit({
                        Opcode::StoreFast,
                        static_cast<std::uint32_t>(local_index(name)),
                    });
                    return;
                }
                emit({
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(name)),
                });
            }

            std::size_t emit_jump(Opcode opcode)
            {
                emit({opcode, 0});
                return instructions_.size() - 1;
            }

            void patch_jump(std::size_t instruction_index, std::size_t target)
            {
                instructions_[instruction_index].operand = static_cast<std::uint32_t>(target);
            }

            struct LoopControl final
            {
                std::size_t start = 0;
                std::size_t continue_target = 0;
                std::size_t break_target = 0;
                std::vector<std::size_t> break_jumps;
                std::vector<std::size_t> continue_jumps;
            };

            SymbolTable collect_function_symbols(const compiler_internal::FunctionDefStmt &function) const
            {
                return analyze_function_symbols(runtime_, function, {&symbols_});
            }

            CodeObject::Flags function_flags(const compiler_internal::FunctionParameters &params) const
            {
                CodeObject::Flags flags = 0;
                if (params.varargs)
                    flags |= CodeObject::FlagVarArgs;
                if (params.varkeywords)
                    flags |= CodeObject::FlagVarKeywords;
                return flags;
            }

            SymbolTable collect_class_symbols(const compiler_internal::ClassDefStmt &klass) const
            {
                return analyze_class_symbols(runtime_, klass);
            }

            std::size_t name_index(const std::string &name) const
            {
                for (std::size_t index = 0; index < names_.size(); ++index)
                {
                    auto *entry = dynamic_cast<StrObject *>(names_[index]);
                    if (entry && entry->value == name)
                        return index;
                }
                raise_syntax_error(&runtime_, "unknown symbol table entry");
            }

            std::size_t ensure_name_index(const std::string &name)
            {
                for (std::size_t index = 0; index < names_.size(); ++index)
                {
                    auto *entry = dynamic_cast<StrObject *>(names_[index]);
                    if (entry && entry->value == name)
                        return index;
                }
                names_.push_back(runtime_.new_str(name));
                return names_.size() - 1;
            }

            std::size_t local_index(const std::string &name) const
            {
                const std::size_t index = symbols_.local_index(name);
                if (index == static_cast<std::size_t>(-1))
                    raise_syntax_error(&runtime_, "unknown local variable entry");
                return index;
            }

            std::size_t deref_index(const std::string &name) const
            {
                const std::size_t index = symbols_.deref_index(name);
                if (index == static_cast<std::size_t>(-1))
                    raise_syntax_error(&runtime_, "unknown closure variable entry");
                return index;
            }

            Runtime &runtime_;
            const SymbolTable &symbols_;
            std::string filename_;
            std::vector<Object *> names_;
            std::vector<Object *> varnames_;
            std::vector<Object *> cellvars_;
            std::vector<Object *> freevars_;
            std::vector<Object *> constants_;
            std::vector<Instruction> instructions_;
            std::vector<std::uint32_t> lines_;
            std::vector<LoopControl> loop_stack_;
            std::uint32_t current_line_ = 1;
            const compiler_internal::Expr *pending_assignment_value_ = nullptr;
            bool deleting_target_ = false;
        };

    }

    CodeObject *compile_eval_input(Runtime &runtime, std::string_view source)
    {
        return compile_eval_input(runtime, source, "<string>");
    }

    CodeObject *compile_eval_input(Runtime &runtime, std::string_view source, std::string filename)
    {
        const std::vector<compiler_internal::Token> tokens =
            compiler_internal::lex_source(runtime, source, filename);
        std::unique_ptr<compiler_internal::Expr> expression =
            compiler_internal::parse_expression(runtime, tokens, filename);
        SymbolTable symbols = collect_symbols(*expression);
        BytecodeBuilder builder(runtime, symbols, filename);
        return builder.build_eval(*expression);
    }

    CodeObject *compile_exec_input(Runtime &runtime, std::string_view source)
    {
        return compile_exec_input(runtime, source, "<string>");
    }

    CodeObject *compile_exec_input(Runtime &runtime, std::string_view source, std::string filename)
    {
        const std::vector<compiler_internal::Token> tokens =
            compiler_internal::lex_source(runtime, source, filename);
        compiler_internal::Module module = compiler_internal::parse_module(runtime, tokens, filename);
        SymbolTable symbols = collect_symbols(module);
        BytecodeBuilder builder(runtime, symbols, filename);
        return builder.build_exec(module);
    }
}
