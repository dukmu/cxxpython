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

            const std::vector<std::string> &names() const noexcept
            {
                return names_;
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

        class BytecodeBuilder final
        {
        public:
            BytecodeBuilder(Runtime &runtime, const SymbolTable &symbols)
                : runtime_(runtime), symbols_(symbols)
            {
            }

            CodeObject *build(const compiler_internal::Expr &expression)
            {
                emit_expression(expression);
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(std::move(constants_), symbols_.make_name_objects(runtime_), std::move(instructions_));
            }

        private:
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
                    emit_name(name->value);
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

            void emit_name(const std::string &name)
            {
                for (std::size_t index = 0; index < symbols_.names().size(); ++index)
                {
                    if (symbols_.names()[index] == name)
                    {
                        instructions_.push_back(Instruction{
                            Opcode::LoadName,
                            static_cast<std::uint32_t>(index),
                        });
                        return;
                    }
                }
                raise_syntax_error(&runtime_, "unknown symbol table entry");
            }

            Runtime &runtime_;
            const SymbolTable &symbols_;
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
        return builder.build(*expression);
    }
}
