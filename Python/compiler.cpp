#include "cxxpython/compiler.hpp"

#include "cxxpython/compiler_internal.hpp"

namespace cxxpython
{
    namespace
    {
        enum class SymbolScope
        {
            Module,
            Function,
            Class,
        };

        class SymbolTable final
        {
        public:
            explicit SymbolTable(SymbolScope scope)
                : scope_(scope)
            {
            }

            SymbolScope scope() const noexcept
            {
                return scope_;
            }

            std::size_t add_name(const std::string &name)
            {
                for (std::size_t index = 0; index < names_.size(); ++index)
                {
                    if (names_[index] == name)
                        return index;
                }
                names_.push_back(name);
                return names_.size() - 1;
            }

            std::vector<Object *> make_name_objects(Runtime &runtime) const
            {
                std::vector<Object *> names;
                names.reserve(names_.size());
                for (const std::string &name : names_)
                    names.push_back(runtime.new_str(name));
                return names;
            }

            std::size_t add_local(const std::string &name)
            {
                for (std::size_t index = 0; index < varnames_.size(); ++index)
                {
                    if (varnames_[index] == name)
                        return index;
                }
                varnames_.push_back(name);
                return varnames_.size() - 1;
            }

            bool is_local(const std::string &name) const
            {
                for (const std::string &entry : varnames_)
                {
                    if (entry == name)
                        return true;
                }
                return false;
            }

            std::size_t local_index(const std::string &name) const
            {
                for (std::size_t index = 0; index < varnames_.size(); ++index)
                {
                    if (varnames_[index] == name)
                        return index;
                }
                return static_cast<std::size_t>(-1);
            }

            std::vector<Object *> make_varname_objects(Runtime &runtime) const
            {
                std::vector<Object *> varnames;
                varnames.reserve(varnames_.size());
                for (const std::string &name : varnames_)
                    varnames.push_back(runtime.new_str(name));
                return varnames;
            }

        private:
            SymbolScope scope_;
            std::vector<std::string> names_;
            std::vector<std::string> varnames_;
        };

        class SymbolCollector final : public compiler_internal::ExprVisitor,
                                      public compiler_internal::TargetVisitor,
                                      public compiler_internal::StmtVisitor
        {
        public:
            SymbolCollector(SymbolTable &symbols)
                : symbols_(symbols)
            {
            }

            void visit(const compiler_internal::IntegerExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::StringExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::UnaryExpr &expression) override
            {
                expression.operand->accept(*this);
            }

            void visit(const compiler_internal::BinaryExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
            }

            void visit(const compiler_internal::CompareExpr &expression) override
            {
                expression.left->accept(*this);
                for (const compiler_internal::Comparison &comparison : expression.comparisons)
                    comparison.right->accept(*this);
            }

            void visit(const compiler_internal::SingletonExpr &expression) override
            {
                (void)expression;
            }

            void visit(const compiler_internal::NameExpr &expression) override
            {
                (void)symbols_.add_name(expression.value);
            }

            void visit(const compiler_internal::AttributeExpr &expression) override
            {
                expression.object->accept(*this);
                (void)symbols_.add_name(expression.name);
            }

            void visit(const compiler_internal::SubscriptExpr &expression) override
            {
                expression.object->accept(*this);
                expression.key->accept(*this);
            }

            void visit(const compiler_internal::ListExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::TupleExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
            }

            void visit(const compiler_internal::DictExpr &expression) override
            {
                for (const compiler_internal::DictItem &item : expression.items)
                {
                    item.key->accept(*this);
                    item.value->accept(*this);
                }
            }

            void visit(const compiler_internal::CallExpr &expression) override
            {
                expression.callee->accept(*this);
                for (const auto &arg : expression.args)
                    arg->accept(*this);
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                (void)symbols_.add_name(target.name);
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                target.object->accept(*this);
                (void)symbols_.add_name(target.name);
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                target.object->accept(*this);
                target.key->accept(*this);
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.target->accept(*this);
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                statement.condition->accept(*this);
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                if (statement.value)
                    statement.value->accept(*this);
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                (void)symbols_.add_name(statement.name);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                for (const auto &base : statement.bases)
                    base->accept(*this);
                (void)symbols_.add_name(statement.name);
            }

        private:
            SymbolTable &symbols_;
        };

        class FunctionLocalCollector final : public compiler_internal::TargetVisitor,
                                             public compiler_internal::StmtVisitor
        {
        public:
            explicit FunctionLocalCollector(SymbolTable &symbols)
                : symbols_(symbols)
            {
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                (void)symbols_.add_local(target.name);
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                (void)target;
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                (void)target;
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
                for (const auto &item : statement.then_body)
                    item->accept(*this);
                for (const auto &item : statement.else_body)
                    item->accept(*this);
            }

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                (void)statement;
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                (void)symbols_.add_local(statement.name);
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                (void)symbols_.add_local(statement.name);
            }

        private:
            SymbolTable &symbols_;
        };

        class BytecodeBuilder final : public compiler_internal::ExprVisitor,
                                      public compiler_internal::TargetVisitor,
                                      public compiler_internal::StmtVisitor
        {
        public:
            BytecodeBuilder(Runtime &runtime, const SymbolTable &symbols, std::string filename)
                : runtime_(runtime),
                  symbols_(symbols),
                  filename_(std::move(filename)),
                  names_(symbols.make_name_objects(runtime)),
                  varnames_(symbols.make_varname_objects(runtime))
            {
            }

            CodeObject *build_eval(const compiler_internal::Expr &expression)
            {
                expression.accept(*this);
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code("<eval>",
                                         filename_,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_));
            }

            CodeObject *build_exec(const compiler_internal::Module &module)
            {
                for (const auto &statement : module.statements)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code("<module>",
                                         filename_,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_));
            }

            CodeObject *build_function(const compiler_internal::FunctionDefStmt &function)
            {
                for (const auto &statement : function.body)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(function.name,
                                         filename_,
                                         static_cast<std::uint32_t>(function.params.size()),
                                         std::move(varnames_),
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_));
            }

            CodeObject *build_class(const compiler_internal::ClassDefStmt &klass)
            {
                for (const auto &statement : klass.body)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(klass.name,
                                         filename_,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_));
            }

            void visit(const compiler_internal::IntegerExpr &expression) override
            {
                emit_constant(runtime_.new_int(expression.value));
            }

            void visit(const compiler_internal::StringExpr &expression) override
            {
                emit_constant(runtime_.new_str(expression.value));
            }

            void visit(const compiler_internal::UnaryExpr &expression) override
            {
                expression.operand->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::UnaryOp,
                    static_cast<std::uint32_t>(expression.op),
                });
            }

            void visit(const compiler_internal::BinaryExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
                instructions_.push_back(Instruction{
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
                        instructions_.push_back(Instruction{Opcode::DupTop, 0});
                        instructions_.push_back(Instruction{Opcode::RotThree, 0});
                    }
                    instructions_.push_back(Instruction{
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
                    instructions_.push_back(Instruction{Opcode::RotTwo, 0});
                    instructions_.push_back(Instruction{Opcode::PopTop, 0});
                    for (std::size_t jump : cleanup_jumps)
                        patch_jump(jump, cleanup_target);
                    patch_jump(end_jump, instructions_.size());
                }
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
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(expression.value))
                {
                    instructions_.push_back(Instruction{
                        Opcode::LoadFast,
                        static_cast<std::uint32_t>(local_index(expression.value)),
                    });
                    return;
                }
                instructions_.push_back(Instruction{
                    Opcode::LoadName,
                    static_cast<std::uint32_t>(name_index(expression.value)),
                });
            }

            void visit(const compiler_internal::AttributeExpr &expression) override
            {
                expression.object->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::LoadAttr,
                    static_cast<std::uint32_t>(name_index(expression.name)),
                });
            }

            void visit(const compiler_internal::SubscriptExpr &expression) override
            {
                expression.object->accept(*this);
                expression.key->accept(*this);
                instructions_.push_back(Instruction{Opcode::BinarySubscr, 0});
            }

            void visit(const compiler_internal::ListExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::BuildList,
                    static_cast<std::uint32_t>(expression.items.size()),
                });
            }

            void visit(const compiler_internal::TupleExpr &expression) override
            {
                for (const auto &item : expression.items)
                    item->accept(*this);
                instructions_.push_back(Instruction{
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
                instructions_.push_back(Instruction{
                    Opcode::BuildMap,
                    static_cast<std::uint32_t>(expression.items.size()),
                });
            }

            void visit(const compiler_internal::CallExpr &expression) override
            {
                expression.callee->accept(*this);
                for (const auto &arg : expression.args)
                    arg->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::Call,
                    static_cast<std::uint32_t>(expression.args.size()),
                });
            }

            void visit(const compiler_internal::NameTarget &target) override
            {
                emit_pending_assignment_value();
                if (symbols_.scope() == SymbolScope::Function && symbols_.is_local(target.name))
                {
                    instructions_.push_back(Instruction{
                        Opcode::StoreFast,
                        static_cast<std::uint32_t>(local_index(target.name)),
                    });
                    return;
                }
                instructions_.push_back(Instruction{
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(target.name)),
                });
            }

            void visit(const compiler_internal::AttributeTarget &target) override
            {
                target.object->accept(*this);
                emit_pending_assignment_value();
                instructions_.push_back(Instruction{
                    Opcode::StoreAttr,
                    static_cast<std::uint32_t>(name_index(target.name)),
                });
            }

            void visit(const compiler_internal::SubscriptTarget &target) override
            {
                target.object->accept(*this);
                target.key->accept(*this);
                emit_pending_assignment_value();
                instructions_.push_back(Instruction{Opcode::StoreSubscr, 0});
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
                instructions_.push_back(Instruction{Opcode::PopTop, 0});
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                PendingAssignmentValue pending(*this, *statement.value);
                statement.target->accept(*this);
            }

            void visit(const compiler_internal::IfStmt &statement) override
            {
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

            void visit(const compiler_internal::ReturnStmt &statement) override
            {
                if (statement.value)
                    statement.value->accept(*this);
                else
                    emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
            }

            void visit(const compiler_internal::FunctionDefStmt &statement) override
            {
                SymbolTable function_symbols = collect_function_symbols(statement);
                BytecodeBuilder function_builder(runtime_, function_symbols, filename_);
                CodeObject *code = function_builder.build_function(statement);
                emit_constant(code);
                instructions_.push_back(Instruction{Opcode::MakeFunction, 0});
                instructions_.push_back(Instruction{
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(statement.name)),
                });
            }

            void visit(const compiler_internal::ClassDefStmt &statement) override
            {
                for (const auto &base : statement.bases)
                    base->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::BuildTuple,
                    static_cast<std::uint32_t>(statement.bases.size()),
                });
                SymbolTable class_symbols = collect_class_symbols(statement);
                BytecodeBuilder class_builder(runtime_, class_symbols, filename_);
                CodeObject *code = class_builder.build_class(statement);
                emit_constant(code);
                instructions_.push_back(Instruction{
                    Opcode::MakeClass,
                    static_cast<std::uint32_t>(name_index(statement.name)),
                });
                instructions_.push_back(Instruction{
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(statement.name)),
                });
            }

        private:
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

            void emit_pending_assignment_value()
            {
                if (!pending_assignment_value_)
                    raise_syntax_error(&runtime_, "assignment target emitted without value");
                pending_assignment_value_->accept(*this);
            }

            void emit_constant(Object *constant)
            {
                constants_.push_back(constant);
                instructions_.push_back(Instruction{
                    Opcode::LoadConst,
                    static_cast<std::uint32_t>(constants_.size() - 1),
                });
            }

            std::size_t emit_jump(Opcode opcode)
            {
                instructions_.push_back(Instruction{opcode, 0});
                return instructions_.size() - 1;
            }

            void patch_jump(std::size_t instruction_index, std::size_t target)
            {
                instructions_[instruction_index].operand = static_cast<std::uint32_t>(target);
            }

            SymbolTable collect_function_symbols(const compiler_internal::FunctionDefStmt &function) const
            {
                SymbolTable symbols(SymbolScope::Function);
                for (const std::string &param : function.params)
                    (void)symbols.add_local(param);
                FunctionLocalCollector locals(symbols);
                for (const auto &statement : function.body)
                    statement->accept(locals);
                SymbolCollector collector(symbols);
                for (const auto &statement : function.body)
                    statement->accept(collector);
                return symbols;
            }

            SymbolTable collect_class_symbols(const compiler_internal::ClassDefStmt &klass) const
            {
                SymbolTable symbols(SymbolScope::Class);
                SymbolCollector collector(symbols);
                for (const auto &statement : klass.body)
                    statement->accept(collector);
                return symbols;
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

            std::size_t local_index(const std::string &name) const
            {
                const std::size_t index = symbols_.local_index(name);
                if (index == static_cast<std::size_t>(-1))
                    raise_syntax_error(&runtime_, "unknown local variable entry");
                return index;
            }

            Runtime &runtime_;
            const SymbolTable &symbols_;
            std::string filename_;
            std::vector<Object *> names_;
            std::vector<Object *> varnames_;
            std::vector<Object *> constants_;
            std::vector<Instruction> instructions_;
            const compiler_internal::Expr *pending_assignment_value_ = nullptr;
        };

        SymbolTable collect_symbols(const compiler_internal::Expr &expression)
        {
            SymbolTable symbols(SymbolScope::Module);
            SymbolCollector collector(symbols);
            expression.accept(collector);
            return symbols;
        }

        SymbolTable collect_symbols(const compiler_internal::Module &module)
        {
            SymbolTable symbols(SymbolScope::Module);
            SymbolCollector collector(symbols);
            for (const auto &statement : module.statements)
                statement->accept(collector);
            return symbols;
        }
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
