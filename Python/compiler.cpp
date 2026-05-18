#include "cxxpython/compiler.hpp"

#include "cxxpython/compiler_internal.hpp"

namespace cxxpython
{
    namespace
    {
        class SymbolTable final
        {
        public:
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

        private:
            std::vector<std::string> names_;
        };

        class SymbolCollector final : public compiler_internal::ExprVisitor,
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

            void visit(const compiler_internal::BinaryAddExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
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

            void visit(const compiler_internal::CallExpr &expression) override
            {
                expression.callee->accept(*this);
                for (const auto &arg : expression.args)
                    arg->accept(*this);
            }

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                (void)symbols_.add_name(statement.target);
                statement.value->accept(*this);
            }

            void visit(const compiler_internal::AssignAttrStmt &statement) override
            {
                statement.object->accept(*this);
                (void)symbols_.add_name(statement.name);
                statement.value->accept(*this);
            }

        private:
            SymbolTable &symbols_;
        };

        class BytecodeBuilder final : public compiler_internal::ExprVisitor,
                                      public compiler_internal::StmtVisitor
        {
        public:
            BytecodeBuilder(Runtime &runtime, const SymbolTable &symbols)
                : runtime_(runtime), names_(symbols.make_name_objects(runtime))
            {
            }

            CodeObject *build_eval(const compiler_internal::Expr &expression, const std::string &filename)
            {
                expression.accept(*this);
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code("<eval>",
                                         filename,
                                         std::move(constants_),
                                         std::move(names_),
                                         std::move(instructions_));
            }

            CodeObject *build_exec(const compiler_internal::Module &module, const std::string &filename)
            {
                for (const auto &statement : module.statements)
                    statement->accept(*this);
                emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code("<module>",
                                         filename,
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

            void visit(const compiler_internal::BinaryAddExpr &expression) override
            {
                expression.left->accept(*this);
                expression.right->accept(*this);
                instructions_.push_back(Instruction{Opcode::BinaryAdd, 0});
            }

            void visit(const compiler_internal::NameExpr &expression) override
            {
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

            void visit(const compiler_internal::ExprStmt &statement) override
            {
                statement.expression->accept(*this);
                instructions_.push_back(Instruction{Opcode::PopTop, 0});
            }

            void visit(const compiler_internal::AssignStmt &statement) override
            {
                statement.value->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::StoreName,
                    static_cast<std::uint32_t>(name_index(statement.target)),
                });
            }

            void visit(const compiler_internal::AssignAttrStmt &statement) override
            {
                statement.object->accept(*this);
                statement.value->accept(*this);
                instructions_.push_back(Instruction{
                    Opcode::StoreAttr,
                    static_cast<std::uint32_t>(name_index(statement.name)),
                });
            }

        private:
            void emit_constant(Object *constant)
            {
                constants_.push_back(constant);
                instructions_.push_back(Instruction{
                    Opcode::LoadConst,
                    static_cast<std::uint32_t>(constants_.size() - 1),
                });
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

            Runtime &runtime_;
            std::vector<Object *> names_;
            std::vector<Object *> constants_;
            std::vector<Instruction> instructions_;
        };

        SymbolTable collect_symbols(const compiler_internal::Expr &expression)
        {
            SymbolTable symbols;
            SymbolCollector collector(symbols);
            expression.accept(collector);
            return symbols;
        }

        SymbolTable collect_symbols(const compiler_internal::Module &module)
        {
            SymbolTable symbols;
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
        BytecodeBuilder builder(runtime, symbols);
        return builder.build_eval(*expression, filename);
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
        BytecodeBuilder builder(runtime, symbols);
        return builder.build_exec(module, filename);
    }
}
