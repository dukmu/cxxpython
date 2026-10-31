// Adapted from CPython 3.12.14 Modules/_randommodule.c (PSF license).
/* MT19937, Copyright (C) 1997-2002 Makoto Matsumoto and Takuji Nishimura.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The names of its contributors may not be used to endorse or promote
 *    products derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */
#include "internal/randommodule.hpp"

#include "cxxpython/execution.hpp"
#include "cxxpython/floatobject.hpp"
#include "cxxpython/moduleobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/type.hpp"
#include "internal/long.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <climits>
#include <cstdint>
#include <memory>
#include <mutex>
#include <sys/random.h>
#include <vector>

namespace cxxpython::core::detail {
namespace {
constexpr std::size_t N = 624, M = 397;
struct RandomState {
    std::mutex mutex;
    std::array<std::uint32_t, N> state{};
    // PyType_GenericNew zeroes the uninitialized state, including its index.
    std::size_t index = 0;
};
struct RandomObject {
    ObjectHeader header;
    const std::shared_ptr<RandomState> state;
    explicit RandomObject(InitialType type) : header(type), state(std::make_shared<RandomState>())
    {}
    void trace(Tracer &) const noexcept {}
};

template <class F> auto locked(Attachment &c, const std::shared_ptr<RandomState> &state, F &&f)
{
    NativeWait wait(c);
    std::lock_guard lock(state->mutex);
    return f(*state);
}

std::uint32_t next(RandomState &s)
{
    auto &mt = s.state;
    if (s.index >= N) {
        for (std::size_t i = 0; i < N; ++i) {
            const std::uint32_t y = (mt[i] & 0x80000000U) | (mt[(i + 1) % N] & 0x7fffffffU);
            mt[i] = mt[(i + M) % N] ^ (y >> 1) ^ ((y & 1U) ? 0x9908b0dfU : 0U);
        }
        s.index = 0;
    }
    std::uint32_t y = mt[s.index++];
    y ^= y >> 11;
    y ^= (y << 7) & 0x9d2c5680U;
    y ^= (y << 15) & 0xefc60000U;
    return y ^ (y >> 18);
}

void seed_array(RandomState &s, const std::vector<std::uint32_t> &key)
{
    auto &mt = s.state;
    mt[0] = 19650218U;
    for (std::size_t i = 1; i < N; ++i)
        mt[i] = 1812433253U * (mt[i - 1] ^ (mt[i - 1] >> 30)) + static_cast<std::uint32_t>(i);
    s.index = N;
    std::size_t i = 1, j = 0;
    for (std::size_t k = std::max(N, key.size()); k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1664525U)) + key[j] +
                static_cast<std::uint32_t>(j);
        if (++i >= N) {
            mt[0] = mt[N - 1];
            i = 1;
        }
        if (++j >= key.size())
            j = 0;
    }
    for (std::size_t k = N - 1; k; --k) {
        mt[i] = (mt[i] ^ ((mt[i - 1] ^ (mt[i - 1] >> 30)) * 1566083941U)) -
                static_cast<std::uint32_t>(i);
        if (++i >= N) {
            mt[0] = mt[N - 1];
            i = 1;
        }
    }
    mt[0] = 0x80000000U;
}

bool receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<RandomObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"expected _random.Random instance", out);
    return false;
}
bool as_index(Attachment &c, Handle value, Integer &number, ReturnSlot &out)
{
    ReturnSlot indexed(c);
    index(c, value, indexed);
    if (indexed.tag() == ResultTag::Raised) {
        out.raised(indexed.exception());
        return false;
    }
    number = integer_value(c.payload<IntObject>(indexed.value()));
    return true;
}

void create(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!c.has_layout<TypeObject>(args[0]) ||
        c.payload<TypeObject>(args[0]).instance_layout.native_family !=
            LayoutDescriptor::of<RandomObject>().native_family) {
        raise_error(c, CoreObject::TypeErrorType, U"Random.__new__ requires a subtype", out);
        return;
    }
    RootScope roots(c);
    c.allocate_instance<RandomObject>(roots.root(args[0]), out);
}

void seed(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto arg = roots.root(args[1]);
    std::vector<std::uint32_t> key;
    if (arg.get(c) == c.builtin(CoreObject::None)) {
        key.resize(N);
        std::size_t done = 0;
        bool failed = false;
        {
            NativeWait wait(c);
            auto *bytes = reinterpret_cast<unsigned char *>(key.data());
            while (done < key.size() * sizeof(key[0])) {
                const auto n = getrandom(bytes + done, key.size() * sizeof(key[0]) - done, 0);
                if (n < 0 && errno == EINTR)
                    continue;
                if (n <= 0) {
                    failed = true;
                    break;
                }
                done += static_cast<std::size_t>(n);
            }
        }
        if (failed) {
            raise_error(c, CoreObject::OSErrorType, U"cannot obtain random seed from system", out);
            return;
        }
    } else {
        Integer number;
        if (c.has_layout<IntObject>(arg.get(c)))
            number = integer_value(c.payload<IntObject>(arg));
        else {
            ReturnSlot hashed(c);
            hash(c, arg, hashed);
            if (hashed.tag() == ResultTag::Raised) {
                out.raised(hashed.exception());
                return;
            }
            number = integer_value(c.payload<IntObject>(hashed.value()));
            if (number < 0)
                number += Integer(1) << (sizeof(std::size_t) * CHAR_BIT);
        }
        if (number < 0)
            number = -number;
        {
            NativeWait wait(c);
            do {
                key.push_back((number & 0xffffffffU).convert_to<std::uint32_t>());
                number >>= 32;
            } while (number != 0);
        }
    }
    const auto state = c.payload<RandomObject>(self).state;
    locked(c, state, [&](RandomState &instance) { seed_array(instance, key); });
    out.ok(c.builtin(CoreObject::None));
}

void initialize(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto positional = roots.root(args[1]);
    const auto keywords = roots.root(args[2]);
    const auto base = roots.root(c.payload<NativeFunctionObject>(function).captures[0]);
    const auto type = roots.root(c.type_of(self.get(c)));
    if (dictionary_size(c, keywords.get(c)) != 0) {
        const auto initializer = find_type_attribute(c, type.get(c), U"__init__");
        const bool inherited =
            initializer && (c.has_layout<MethodDescriptorObject>(initializer->value)
                                ? c.payload<MethodDescriptorObject>(initializer->value).function
                                : initializer->value) == function.get(c);
        // CPython random_init rejects keywords only for the base type or a
        // subtype whose effective tp_init is still this native initializer.
        if (type.get(c) == base.get(c) || inherited) {
            raise_error(c, CoreObject::TypeErrorType, U"Random() takes no keyword arguments", out);
            return;
        }
    }
    const auto count = c.payload<TupleObject>(positional).items.size();
    if (count > 1) {
        raise_error(c, CoreObject::TypeErrorType, U"Random() requires 0 or 1 argument", out);
        return;
    }
    const std::array seed_args{self.get(c), count ? c.payload<TupleObject>(positional).items[0]
                                                  : c.builtin(CoreObject::None)};
    seed(c, {}, seed_args, out);
}

void random_value(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto state = c.payload<RandomObject>(roots.root(args[0])).state;
    const double value = locked(c, state, [&](RandomState &self) {
        const auto a = next(self) >> 5, b = next(self) >> 6;
        return (a * 67108864.0 + b) * (1.0 / 9007199254740992.0);
    });
    make_float(c, value, out);
}

void getrandbits(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    Integer bits;
    if (!as_index(c, roots.root(args[1]), bits, out))
        return;
    if (bits < INT_MIN || bits > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return;
    }
    const int count = bits.convert_to<int>();
    if (count < 0) {
        raise_error(c, CoreObject::ValueErrorType, U"number of bits must be non-negative", out);
        return;
    }
    if (count == 0) {
        out.ok(c.builtin(CoreObject::IntZero));
        return;
    }
    std::vector<std::uint32_t> words((static_cast<std::size_t>(count) + 31) / 32);
    const auto state = c.payload<RandomObject>(self).state;
    locked(c, state, [&](RandomState &instance) {
        int remaining = count;
        for (auto &word : words) {
            word = next(instance);
            if (remaining < 32)
                word >>= 32 - remaining;
            remaining -= 32;
        }
    });
    Integer result = 0;
    {
        NativeWait wait(c);
        boost::multiprecision::import_bits(result, words.begin(), words.end(), 32, false);
    }
    publish_integer(c, std::move(result), out);
}

void getstate(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto native = c.payload<RandomObject>(roots.root(args[0])).state;
    std::array<std::uint32_t, N> state;
    std::size_t index_value = 0;
    locked(c, native, [&](RandomState &self) {
        state = self.state;
        index_value = self.index;
    });
    std::vector<Handle> items;
    items.reserve(N + 1);
    ReturnSlot item(c);
    for (auto word : state) {
        make_int(c, word, item);
        if (item.tag() == ResultTag::Raised) {
            out.raised(item.exception());
            return;
        }
        items.push_back(roots.root(item.value()));
    }
    make_int(c, static_cast<std::int64_t>(index_value), item);
    if (item.tag() == ResultTag::Raised) {
        out.raised(item.exception());
        return;
    }
    items.push_back(roots.root(item.value()));
    std::vector<Value> values;
    values.reserve(items.size());
    for (auto handle : items)
        values.push_back(handle.get(c));
    cxxpython::core::make_tuple(c, values, out);
}

void setstate(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    if (!receiver(c, args[0], out))
        return;
    RootScope roots(c);
    const auto self = roots.root(args[0]);
    const auto tuple = roots.root(args[1]);
    if (!c.has_layout<TupleObject>(tuple.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"state vector must be a tuple", out);
        return;
    }
    if (c.payload<TupleObject>(tuple).items.size() != N + 1) {
        raise_error(c, CoreObject::ValueErrorType, U"state vector is the wrong size", out);
        return;
    }
    std::array<std::uint32_t, N> state;
    Integer number;
    for (std::size_t i = 0; i <= N; ++i) {
        if (i < N) {
            const auto item = roots.root(c.payload<TupleObject>(tuple).items[i]);
            if (!c.has_layout<IntObject>(item.get(c))) {
                raise_error(c, CoreObject::TypeErrorType, U"an integer is required", out);
                return;
            }
            number = integer_value(c.payload<IntObject>(item));
            if (number < 0 || number > ULONG_MAX) {
                raise_error(c, CoreObject::OverflowErrorType,
                            U"Python int too large to convert to C unsigned long", out);
                return;
            }
            state[i] = (number & 0xffffffffU).convert_to<std::uint32_t>();
        } else {
            if (!as_index(c, roots.root(c.payload<TupleObject>(tuple).items[i]), number, out))
                return;
            if (number < LONG_MIN || number > LONG_MAX) {
                raise_error(c, CoreObject::OverflowErrorType,
                            U"Python int too large to convert to C long", out);
                return;
            }
        }
    }
    if (number < 0 || number > N) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid state", out);
        return;
    }
    const auto position = number.convert_to<std::size_t>();
    const auto native = c.payload<RandomObject>(self).state;
    locked(c, native, [&](RandomState &instance) {
        instance.state = state;
        instance.index = position;
    });
    out.ok(c.builtin(CoreObject::None));
}
} // namespace

void initialize_random(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    std::vector<Handle> core;
    for (std::size_t i = 0; i < static_cast<std::size_t>(CoreObject::Count); ++i)
        core.push_back(roots.root(c.builtin(static_cast<CoreObject>(i))));
    NativeTypeSpec spec{{"Random"}, LayoutDescriptor::of<RandomObject>()};
    spec.type.module = U"_random";
    spec.type.immutable = true;
    spec.type.subclassable = true;
    ReturnSlot value(c), wrapper(c), key(c);
    create_native_type(c, spec, value);
    if (value.tag() == ResultTag::Raised) {
        out.raised(value.exception());
        return;
    }
    const auto type = roots.root(value.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto binary = unary;
    binary.parameters.push_back({U"value", ParameterKind::PositionalOnly});
    auto optional = unary;
    optional.parameters.push_back(
        {U"value", ParameterKind::PositionalOnly, c.builtin(CoreObject::None)});
    auto constructor = unary;
    constructor.varargs = U"args";
    constructor.varkwargs = U"kwargs";
    install_native_attribute(c, core, type, U"__new__", constructor, create);
    auto dictionary = *TypeView(c, type).dictionary();
    const auto new_function = roots.root(*name_lookup(c, dictionary, U"__new__"));
    c.allocate_instance<StaticMethodObject>(
        core[static_cast<std::size_t>(CoreObject::StaticMethodType)], wrapper, new_function.get(c));
    c.allocate_instance<StrObject>(core[static_cast<std::size_t>(CoreObject::StrType)], key,
                                   U"__new__");
    name_store(c, dictionary, key.value(), wrapper.value());
    install_native_attribute(c, core, type, U"__init__", constructor, initialize, {type.get(c)});
    install_native_attribute(c, core, type, U"seed", optional, seed);
    install_native_attribute(c, core, type, U"random", unary, random_value);
    install_native_attribute(c, core, type, U"getrandbits", binary, getrandbits);
    install_native_attribute(c, core, type, U"getstate", unary, getstate);
    install_native_attribute(c, core, type, U"setstate", binary, setstate);
    const auto dict = roots.root(module_dict(c, module.get(c)));
    make_str(c, U"Random", key);
    if (key.tag() == ResultTag::Raised) {
        out.raised(key.exception());
        return;
    }
    dict_store(c, dict, roots.root(key.value()), type, out);
    if (out.tag() != ResultTag::Raised)
        out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
