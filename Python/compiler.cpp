#include "cxxpython/compiler.hpp"

#include "cxxpython/compiler_internal.hpp"

namespace cxxpython
{
    namespace
    {
        class BytecodeBuilder final
        {
        public:
            explicit BytecodeBuilder(Runtime &runtime)
                : runtime_(runtime)
            {
            }

            CodeObject *build(const compiler_internal::Expr &expression)
            {
                emit_expression(expression);
                instructions_.push_back(Instruction{Opcode::ReturnValue, 0});
                return runtime_.new_code(std::move(constants_), std::move(instructions_));
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

            Runtime &runtime_;
            std::vector<Object *> constants_;
            std::vector<Instruction> instructions_;
        };
    }

    CodeObject *compile_eval_input(Runtime &runtime, std::string_view source)
    {
        const std::vector<compiler_internal::Token> tokens = compiler_internal::lex_source(runtime, source);
        std::unique_ptr<compiler_internal::Expr> expression =
            compiler_internal::parse_expression(runtime, tokens);
        BytecodeBuilder builder(runtime);
        return builder.build(*expression);
    }
}
