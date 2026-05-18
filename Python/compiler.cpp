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

        void collect_names(Runtime &runtime, const compiler_internal::Expr &expression, SymbolTable &symbols)
        {
            if (dynamic_cast<const compiler_internal::IntegerExpr *>(&expression) != nullptr)
                return;
            if (dynamic_cast<const compiler_internal::StringExpr *>(&expression) != nullptr)
                return;
            if (auto *name = dynamic_cast<const compiler_internal::NameExpr *>(&expression))
            {
                (void)symbols.add_name(name->value);
                return;
            }
            if (auto *binary_add = dynamic_cast<const compiler_internal::BinaryAddExpr *>(&expression))
            {
                collect_names(runtime, *binary_add->left, symbols);
                collect_names(runtime, *binary_add->right, symbols);
                return;
            }
            raise_syntax_error(&runtime, "unsupported expression");
        }

        void collect_names(Runtime &runtime, const compiler_internal::Stmt &statement, SymbolTable &symbols)
        {
            if (auto *expr_stmt = dynamic_cast<const compiler_internal::ExprStmt *>(&statement))
            {
                collect_names(runtime, *expr_stmt->expression, symbols);
                return;
            }
            if (auto *assign_stmt = dynamic_cast<const compiler_internal::AssignStmt *>(&statement))
            {
                (void)symbols.add_name(assign_stmt->target);
                collect_names(runtime, *assign_stmt->value, symbols);
                return;
            }
            raise_syntax_error(&runtime, "unsupported statement");
        }

        class BytecodeBuilder final
        {
        public:
            BytecodeBuilder(Runtime &runtime, const SymbolTable &symbols)
                : runtime_(runtime), names_(symbols.make_name_objects(runtime))
            {
            }

            CodeObject *build_eval(const compiler_internal::Expr &expression)
            {
                emit_expression(expression);
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(std::move(constants_), std::move(names_), std::move(instructions_));
            }

            CodeObject *build_exec(const compiler_internal::Module &module)
            {
                for (const auto &statement : module.statements)
                    emit_statement(*statement);
                emit_constant(runtime_.none());
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(std::move(constants_), std::move(names_), std::move(instructions_));
            }

        private:
            void emit_statement(const compiler_internal::Stmt &statement)
            {
                if (auto *expr_stmt = dynamic_cast<const compiler_internal::ExprStmt *>(&statement))
                {
                    emit_expression(*expr_stmt->expression);
                    instructions_.push_back(Instruction{Opcode::PopTop, 0});
                    return;
                }
                if (auto *assign_stmt = dynamic_cast<const compiler_internal::AssignStmt *>(&statement))
                {
                    emit_expression(*assign_stmt->value);
                    instructions_.push_back(Instruction{
                        Opcode::StoreName,
                        static_cast<std::uint32_t>(name_index(assign_stmt->target)),
                    });
                    return;
                }
                raise_syntax_error(&runtime_, "unsupported statement");
            }

            void emit_expression(const compiler_internal::Expr &expression)
            {
                if (auto *integer = dynamic_cast<const compiler_internal::IntegerExpr *>(&expression))
                {
                    emit_constant(runtime_.new_int(integer->value));
                    return;
                }
                if (auto *string = dynamic_cast<const compiler_internal::StringExpr *>(&expression))
                {
                    emit_constant(runtime_.new_str(string->value));
                    return;
                }
                if (auto *name = dynamic_cast<const compiler_internal::NameExpr *>(&expression))
                {
                    instructions_.push_back(Instruction{
                        Opcode::LoadName,
                        static_cast<std::uint32_t>(name_index(name->value)),
                    });
                    return;
                }
                if (auto *binary_add = dynamic_cast<const compiler_internal::BinaryAddExpr *>(&expression))
                {
                    emit_expression(*binary_add->left);
                    emit_expression(*binary_add->right);
                    instructions_.push_back(Instruction{Opcode::BinaryAdd, 0});
                    return;
                }
                raise_syntax_error(&runtime_, "unsupported expression");
            }

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
    }

    CodeObject *compile_eval_input(Runtime &runtime, std::string_view source)
    {
        const std::vector<compiler_internal::Token> tokens = compiler_internal::lex_source(runtime, source);
        std::unique_ptr<compiler_internal::Expr> expression =
            compiler_internal::parse_expression(runtime, tokens);
        SymbolTable symbols;
        collect_names(runtime, *expression, symbols);
        BytecodeBuilder builder(runtime, symbols);
        return builder.build_eval(*expression);
    }

    CodeObject *compile_exec_input(Runtime &runtime, std::string_view source)
    {
        const std::vector<compiler_internal::Token> tokens = compiler_internal::lex_source(runtime, source);
        compiler_internal::Module module = compiler_internal::parse_module(runtime, tokens);
        SymbolTable symbols;
        for (const auto &statement : module.statements)
            collect_names(runtime, *statement, symbols);
        BytecodeBuilder builder(runtime, symbols);
        return builder.build_exec(module);
    }
}
