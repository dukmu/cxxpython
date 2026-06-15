#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "cxxpython/compiler_internal.hpp"

namespace cxxpython
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
        explicit SymbolTable(SymbolScope scope);

        SymbolScope scope() const noexcept;

        std::size_t add_name(const std::string &name);
        std::vector<StrObject *> make_name_objects(Runtime &runtime) const;
        const std::vector<std::string> &names() const noexcept;

        std::size_t add_local(const std::string &name);
        bool is_local(const std::string &name) const;
        std::size_t local_index(const std::string &name) const;

        std::size_t add_cellvar(const std::string &name);
        bool is_cellvar(const std::string &name) const;
        std::size_t cell_index(const std::string &name) const;

        std::size_t add_freevar(const std::string &name);
        bool is_freevar(const std::string &name) const;
        std::size_t free_index(const std::string &name) const;

        bool resolves_lexically(const std::string &name) const;
        std::size_t deref_index(const std::string &name) const;

        const std::vector<std::string> &freevars() const noexcept;
        const std::vector<std::string> &cellvars() const noexcept;

        void add_global(const std::string &name);
        bool is_global(const std::string &name) const;

        void add_nonlocal(const std::string &name);
        bool is_nonlocal(const std::string &name) const;
        const std::vector<std::string> &nonlocals() const noexcept;

        std::vector<StrObject *> make_varname_objects(Runtime &runtime) const;
        std::vector<StrObject *> make_cellvar_objects(Runtime &runtime) const;
        std::vector<StrObject *> make_freevar_objects(Runtime &runtime) const;

    private:
        SymbolScope scope_;
        std::vector<std::string> names_;
        std::vector<std::string> varnames_;
        std::vector<std::string> cellvars_;
        std::vector<std::string> freevars_;
        std::vector<std::string> globals_;
        std::vector<std::string> nonlocals_;
    };

    SymbolTable analyze_function_symbols(Runtime &runtime,
                                         const compiler_internal::FunctionDefStmt &function,
                                         const std::vector<const SymbolTable *> &enclosing);
    SymbolTable analyze_lambda_symbols(Runtime &runtime,
                                       const compiler_internal::LambdaExpr &lambda,
                                       const std::vector<const SymbolTable *> &enclosing);
    SymbolTable analyze_class_symbols(Runtime &runtime,
                                      const compiler_internal::ClassDefStmt &klass);

    SymbolTable collect_symbols(const compiler_internal::Expr &expression);
    SymbolTable collect_symbols(const compiler_internal::Module &module);
}
