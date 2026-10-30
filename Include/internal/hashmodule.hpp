#pragma once

#include "../../Modules/_blake2/impl/blake2.h"
#include "cxxpython/objects.hpp"
#include <openssl/evp.h>

namespace cxxpython::core {
struct HashObject {
    struct State {
        std::mutex mutex;
        EVP_MD_CTX *evp = nullptr;
        blake2b_state b{};
        blake2s_state s{};
        ~State()
        {
            EVP_MD_CTX_free(evp);
        }
    };
    ObjectHeader header;
    const std::unique_ptr<State> state;
    const std::string name;
    const int digest_size;
    const int block_size;
    const bool xof;
    const int blake;
    HashObject(InitialType type, std::string algorithm, int digest, int block, bool is_xof,
               int blake_kind)
        : header(type), state(std::make_unique<State>()), name(std::move(algorithm)),
          digest_size(digest), block_size(block), xof(is_xof), blake(blake_kind)
    {}
    void trace(Tracer &) const noexcept {}
    std::size_t external_bytes() const noexcept
    {
        return sizeof(State) + name.capacity() + 1;
    }
};
namespace detail {
void install_hashes(Attachment &, std::span<const Handle>);
void initialize_hashlib(Attachment &, Handle, ReturnSlot &);
void initialize_blake2(Attachment &, Handle, ReturnSlot &);
} // namespace detail
} // namespace cxxpython::core
