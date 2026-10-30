#include "cxxpython/import.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "cxxpython/setobject.hpp"
#include "internal/hashmodule.hpp"

#include <algorithm>
#include <array>
#include <climits>
#include <cstring>

namespace cxxpython::core::detail {
namespace {
bool bytes(Attachment &c, Handle value, std::string &result, ReturnSlot &out)
{
    if (buffer_bytes(c, value, result, out))
        return true;
    if (out.tag() == ResultTag::Raised)
        return false;
    raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
    return false;
}
bool number(Attachment &c, Value value, std::int64_t &result, ReturnSlot &out)
{
    RootScope roots(c);
    index(c, roots.root(value), out);
    if (out.tag() == ResultTag::Raised)
        return false;
    if (!int_fits_signed(c, out.value(), result)) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return false;
    }
    return true;
}
bool text(Attachment &c, Value value, std::string &result, ReturnSlot &out)
{
    if (!c.has_layout<StrObject>(value)) {
        raise_error(c, CoreObject::TypeErrorType, U"name must be a string", out);
        return false;
    }
    for (char32_t ch : c.payload<StrObject>(value).value) {
        if (ch == 0 || ch > 127) {
            raise_error(c, CoreObject::ValueErrorType, U"invalid hash name", out);
            return false;
        }
        result.push_back(static_cast<char>(ch));
    }
    return true;
}
void error(Attachment &c, ReturnSlot &out)
{
    raise_error(c, CoreObject::ValueErrorType, U"OpenSSL hash operation failed", out);
}
void new_evp(Attachment &c, std::string name, Value data, bool security, ReturnSlot &out)
{
    RootScope roots(c);
    std::string input;
    if (!bytes(c, roots.root(data), input, out))
        return;
    std::string openssl_name = name;
    if (openssl_name.starts_with("sha3_"))
        openssl_name[4] = '-';
    if (openssl_name == "shake_128")
        openssl_name = "shake128";
    if (openssl_name == "shake_256")
        openssl_name = "shake256";
    std::unique_ptr<EVP_MD, void (*)(EVP_MD *)> md(
        EVP_MD_fetch(nullptr, openssl_name.c_str(), security ? nullptr : "-fips"), EVP_MD_free);
    if (!md) {
        raise_error(c, CoreObject::ValueErrorType, U"unsupported hash type", out);
        return;
    }
    const bool xof = (EVP_MD_get_flags(md.get()) & EVP_MD_FLAG_XOF) != 0;
    const int size = xof ? 0 : EVP_MD_get_size(md.get());
    const int block = EVP_MD_get_block_size(md.get());
    const char *canonical = EVP_MD_get0_name(md.get());
    std::string normalized = name;
    if (canonical && (name == "SHAKE128" || name == "shake128"))
        normalized = "shake_128";
    if (canonical && (name == "SHAKE256" || name == "shake256"))
        normalized = "shake_256";
    for (auto &ch : normalized)
        if (ch >= 'A' && ch <= 'Z')
            ch += 'a' - 'A';
    std::unique_ptr<EVP_MD_CTX, void (*)(EVP_MD_CTX *)> state(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    bool ok;
    {
        NativeWait wait(c);
        ok = state && EVP_DigestInit_ex(state.get(), md.get(), nullptr) == 1 &&
             EVP_DigestUpdate(state.get(), input.data(), input.size()) == 1;
    }
    if (!ok) {
        error(c, out);
        return;
    }
    auto type = roots.root(c.builtin(xof ? CoreObject::HashXofType : CoreObject::HashType));
    c.allocate_instance<HashObject>(type, out, std::move(normalized), size, block, xof, 0);
    if (out.tag() != ResultTag::Raised)
        c.payload<HashObject>(out.value()).state->evp = state.release();
}
template <int Id>
void constructor(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    static constexpr const char *names[] = {"md5",      "sha1",     "sha224",    "sha256",
                                            "sha384",   "sha512",   "sha3_224",  "sha3_256",
                                            "sha3_384", "sha3_512", "shake_128", "shake_256"};
    RootScope roots(c);
    ReturnSlot security(c);
    truth(c, roots.root(args[1]), security);
    if (security.tag() == ResultTag::Raised) {
        out.raised(security.exception());
        return;
    }
    new_evp(c, names[Id], args[0], security.value() == c.builtin(CoreObject::True), out);
}
void new_hash(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    std::string name;
    if (!text(c, args[0], name, out))
        return;
    RootScope roots(c);
    ReturnSlot security(c);
    truth(c, roots.root(args[2]), security);
    if (security.tag() == ResultTag::Raised) {
        out.raised(security.exception());
        return;
    }
    new_evp(c, name, args[1], security.value() == c.builtin(CoreObject::True), out);
}
bool hash_receiver(Attachment &c, Value value, ReturnSlot &out)
{
    if (c.has_layout<HashObject>(value))
        return true;
    raise_error(c, CoreObject::TypeErrorType, U"hash descriptor requires a hash object", out);
    return false;
}
void update(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!hash_receiver(c, args[0], out))
        return;
    std::string input;
    if (!bytes(c, roots.root(args[1]), input, out))
        return;
    auto &obj = c.payload<HashObject>(self);
    int ok;
    {
        NativeWait wait(c);
        std::lock_guard lock(obj.state->mutex);
        if (obj.blake == 1)
            ok = blake2b_update(&obj.state->b, reinterpret_cast<const uint8_t *>(input.data()),
                                input.size()) == 0;
        else if (obj.blake == 2)
            ok = blake2s_update(&obj.state->s, reinterpret_cast<const uint8_t *>(input.data()),
                                input.size()) == 0;
        else
            ok = EVP_DigestUpdate(obj.state->evp, input.data(), input.size()) == 1;
    }
    if (!ok)
        return error(c, out);
    out.ok(c.builtin(CoreObject::None));
}
void copy(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!hash_receiver(c, args[0], out))
        return;
    auto &obj = c.payload<HashObject>(self);
    ReturnSlot result(c);
    c.allocate_instance<HashObject>(roots.root(c.type_of(args[0])), result, obj.name,
                                    obj.digest_size, obj.block_size, obj.xof, obj.blake);
    if (result.tag() == ResultTag::Raised) {
        out.raised(result.exception());
        return;
    }
    auto target = roots.root(result.value());
    auto &dest = c.payload<HashObject>(target);
    if (!obj.blake)
        dest.state->evp = EVP_MD_CTX_new();
    if (!obj.blake && !dest.state->evp)
        return error(c, out);
    int ok = 1;
    {
        NativeWait wait(c);
        std::lock_guard lock(obj.state->mutex);
        if (obj.blake == 1)
            dest.state->b = obj.state->b;
        else if (obj.blake == 2)
            dest.state->s = obj.state->s;
        else
            ok = EVP_MD_CTX_copy_ex(dest.state->evp, obj.state->evp);
    }
    if (!ok)
        return error(c, out);
    out.ok(target.get(c));
}
template <bool Hex> void digest(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    auto self = roots.root(args[0]);
    if (!hash_receiver(c, args[0], out))
        return;
    auto &obj = c.payload<HashObject>(self);
    std::int64_t length = obj.digest_size;
    if (obj.xof && !number(c, args[1], length, out))
        return;
    if (length < 0 || length > INT_MAX) {
        raise_error(c, CoreObject::ValueErrorType, U"length must be non-negative and fit in an int",
                    out);
        return;
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    EVP_MD_CTX *tmp = obj.blake ? nullptr : EVP_MD_CTX_new();
    if (!obj.blake && !tmp)
        return error(c, out);
    int ok = 1;
    {
        NativeWait wait(c);
        std::lock_guard lock(obj.state->mutex);
        if (obj.blake == 1) {
            auto state = obj.state->b;
            ok = blake2b_final(&state, reinterpret_cast<uint8_t *>(result.data()), result.size()) ==
                 0;
        } else if (obj.blake == 2) {
            auto state = obj.state->s;
            ok = blake2s_final(&state, reinterpret_cast<uint8_t *>(result.data()), result.size()) ==
                 0;
        } else
            ok = EVP_MD_CTX_copy_ex(tmp, obj.state->evp);
    }
    if (!obj.blake && ok) {
        if (obj.xof)
            ok = EVP_DigestFinalXOF(tmp, reinterpret_cast<unsigned char *>(result.data()),
                                    result.size());
        else {
            unsigned int n = 0;
            ok = EVP_DigestFinal_ex(tmp, reinterpret_cast<unsigned char *>(result.data()), &n);
        }
    }
    EVP_MD_CTX_free(tmp);
    if (!ok)
        return error(c, out);
    if constexpr (!Hex)
        make_bytes(c, std::move(result), out);
    else {
        constexpr char32_t digits[] = U"0123456789abcdef";
        std::u32string hex;
        hex.reserve(result.size() * 2);
        for (unsigned char ch : result) {
            hex.push_back(digits[ch >> 4]);
            hex.push_back(digits[ch & 15]);
        }
        make_str(c, std::move(hex), out);
    }
}
void get_name(Attachment &c, Value value, ReturnSlot &out)
{
    if (!hash_receiver(c, value, out))
        return;
    const auto &name = c.payload<HashObject>(value).name;
    make_str(c, std::u32string(name.begin(), name.end()), out);
}
template <bool Block> void get_size(Attachment &c, Value value, ReturnSlot &out)
{
    if (!hash_receiver(c, value, out))
        return;
    auto &obj = c.payload<HashObject>(value);
    make_int(c, Block ? obj.block_size : obj.digest_size, out);
}

template <bool Small>
void blake_new(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string data, key, salt, person;
    if (!bytes(c, roots.root(args[0]), data, out) || !bytes(c, roots.root(args[2]), key, out) ||
        !bytes(c, roots.root(args[3]), salt, out) || !bytes(c, roots.root(args[4]), person, out))
        return;
    std::int64_t v[7];
    for (int i = 0; i < 7; ++i) {
        const int arg = i == 0 ? 1 : i + 4;
        if (!number(c, args[arg], v[i], out))
            return;
    }
    const int max = Small ? static_cast<int>(BLAKE2S_OUTBYTES) : static_cast<int>(BLAKE2B_OUTBYTES);
    const int saltmax =
        Small ? static_cast<int>(BLAKE2S_SALTBYTES) : static_cast<int>(BLAKE2B_SALTBYTES);
    if (v[0] < 1 || v[0] > max || key.size() > static_cast<std::size_t>(max) ||
        salt.size() > static_cast<std::size_t>(saltmax) ||
        person.size() > static_cast<std::size_t>(saltmax) || v[1] < 0 || v[1] > 255 || v[2] < 1 ||
        v[2] > 255 || v[3] < 0 || v[3] > UINT32_MAX || v[4] < 0 ||
        (Small && static_cast<std::uint64_t>(v[4]) > 0xffffffffffffULL) || v[5] < 0 || v[5] > 255 ||
        v[6] < 0 || v[6] > max) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid BLAKE2 parameter", out);
        return;
    }
    truth(c, roots.root(args[11]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    bool last = out.value() == c.builtin(CoreObject::True);
    truth(c, roots.root(args[12]), out);
    if (out.tag() == ResultTag::Raised)
        return;
    auto type = roots.root(c.builtin(Small ? CoreObject::Blake2sType : CoreObject::Blake2bType));
    c.allocate_instance<HashObject>(
        type, out, Small ? "blake2s" : "blake2b", static_cast<int>(v[0]),
        Small ? static_cast<int>(BLAKE2S_BLOCKBYTES) : static_cast<int>(BLAKE2B_BLOCKBYTES), false,
        Small ? 2 : 1);
    if (out.tag() == ResultTag::Raised)
        return;
    auto self = roots.root(out.value());
    auto &obj = c.payload<HashObject>(self);
    int ok;
    if constexpr (Small) {
        blake2s_param p{};
        p.digest_length = v[0];
        p.key_length = key.size();
        p.fanout = v[1];
        p.depth = v[2];
        auto *leaf = reinterpret_cast<uint8_t *>(&p.leaf_length);
        for (int i = 0; i < 4; ++i)
            leaf[i] = (static_cast<std::uint64_t>(v[3]) >> (8 * i)) & 255;
        for (int i = 0; i < 6; ++i)
            p.node_offset[i] = (static_cast<std::uint64_t>(v[4]) >> (8 * i)) & 255;
        p.node_depth = v[5];
        p.inner_length = v[6];
        std::memcpy(p.salt, salt.data(), salt.size());
        std::memcpy(p.personal, person.data(), person.size());
        ok = blake2s_init_param(&obj.state->s, &p) == 0;
        obj.state->s.last_node = last;
        if (!key.empty()) {
            std::array<uint8_t, BLAKE2S_BLOCKBYTES> block{};
            std::memcpy(block.data(), key.data(), key.size());
            ok &= blake2s_update(&obj.state->s, block.data(), block.size()) == 0;
        }
        ok &= blake2s_update(&obj.state->s, reinterpret_cast<const uint8_t *>(data.data()),
                             data.size()) == 0;
    } else {
        blake2b_param p{};
        p.digest_length = v[0];
        p.key_length = key.size();
        p.fanout = v[1];
        p.depth = v[2];
        auto *leaf = reinterpret_cast<uint8_t *>(&p.leaf_length);
        auto *offset = reinterpret_cast<uint8_t *>(&p.node_offset);
        for (int i = 0; i < 4; ++i)
            leaf[i] = (static_cast<std::uint64_t>(v[3]) >> (8 * i)) & 255;
        for (int i = 0; i < 8; ++i)
            offset[i] = (static_cast<std::uint64_t>(v[4]) >> (8 * i)) & 255;
        p.node_depth = v[5];
        p.inner_length = v[6];
        std::memcpy(p.salt, salt.data(), salt.size());
        std::memcpy(p.personal, person.data(), person.size());
        ok = blake2b_init_param(&obj.state->b, &p) == 0;
        obj.state->b.last_node = last;
        if (!key.empty()) {
            std::array<uint8_t, BLAKE2B_BLOCKBYTES> block{};
            std::memcpy(block.data(), key.data(), key.size());
            ok &= blake2b_update(&obj.state->b, block.data(), block.size()) == 0;
        }
        ok &= blake2b_update(&obj.state->b, reinterpret_cast<const uint8_t *>(data.data()),
                             data.size()) == 0;
    }
    if (!ok)
        return error(c, out);
    out.ok(self.get(c));
}
template <bool Small>
void blake_type_new(Attachment &c, Handle function, std::span<const Value> args, ReturnSlot &out)
{
    if (args[0] != c.builtin(Small ? CoreObject::Blake2sType : CoreObject::Blake2bType)) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid BLAKE2 type", out);
        return;
    }
    blake_new<Small>(c, function, args.subspan(1), out);
}
void pbkdf2(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string name, password, salt;
    if (!text(c, args[0], name, out) || !bytes(c, roots.root(args[1]), password, out) ||
        !bytes(c, roots.root(args[2]), salt, out))
        return;
    std::int64_t iterations, length = 0;
    if (!number(c, args[3], iterations, out))
        return;
    std::unique_ptr<EVP_MD, void (*)(EVP_MD *)> md(EVP_MD_fetch(nullptr, name.c_str(), nullptr),
                                                   EVP_MD_free);
    if (!md) {
        raise_error(c, CoreObject::ValueErrorType, U"unsupported hash type", out);
        return;
    }
    if (args[4] == c.builtin(CoreObject::None))
        length = EVP_MD_get_size(md.get());
    else if (!number(c, args[4], length, out)) {
        return;
    }
    if (iterations < 1 || iterations > INT_MAX || length < 1 || length > INT_MAX ||
        password.size() > INT_MAX || salt.size() > INT_MAX) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid PBKDF2 parameters", out);
        return;
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    int ok;
    {
        NativeWait wait(c);
        ok = PKCS5_PBKDF2_HMAC(password.data(), static_cast<int>(password.size()),
                               reinterpret_cast<const unsigned char *>(salt.data()),
                               static_cast<int>(salt.size()), static_cast<int>(iterations),
                               md.get(), static_cast<int>(length),
                               reinterpret_cast<unsigned char *>(result.data()));
    }
    if (!ok)
        return error(c, out);
    make_bytes(c, std::move(result), out);
}
void scrypt(Attachment &c, Handle, std::span<const Value> args, ReturnSlot &out)
{
    RootScope roots(c);
    std::string password, salt;
    if (!bytes(c, roots.root(args[0]), password, out) || !bytes(c, roots.root(args[1]), salt, out))
        return;
    std::int64_t n, r, p, maxmem, length;
    if (!number(c, args[2], n, out) || !number(c, args[3], r, out) || !number(c, args[4], p, out) ||
        !number(c, args[5], maxmem, out) || !number(c, args[6], length, out))
        return;
    if (n < 2 || (n & (n - 1)) || r < 1 || p < 1 || maxmem < 0 || maxmem > INT_MAX || length < 1 ||
        length > INT_MAX) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid scrypt parameters", out);
        return;
    }
    std::string result(static_cast<std::size_t>(length), '\0');
    int ok;
    {
        NativeWait wait(c);
        ok = EVP_PBE_scrypt(password.data(), password.size(),
                            reinterpret_cast<const unsigned char *>(salt.data()), salt.size(), n, r,
                            p, maxmem, reinterpret_cast<unsigned char *>(result.data()),
                            result.size());
    }
    if (!ok)
        return error(c, out);
    make_bytes(c, std::move(result), out);
}
} // namespace

void install_hashes(Attachment &c, std::span<const Handle> core)
{
    auto at = [&](CoreObject id) { return core[static_cast<std::size_t>(id)]; };
    RootScope roots(c);
    ReturnSlot empty(c), zero(c), one(c), maxb(c), maxs(c), key(c), wrapper(c), constant(c);
    c.allocate_instance<BytesObject>(at(CoreObject::BytesType), empty, std::string{});
    auto e = roots.root(empty.value());
    c.allocate_instance<IntObject>(at(CoreObject::IntType), zero, 0);
    auto z = roots.root(zero.value());
    c.allocate_instance<IntObject>(at(CoreObject::IntType), one, 1);
    auto o = roots.root(one.value());
    c.allocate_instance<IntObject>(at(CoreObject::IntType), maxb, BLAKE2B_OUTBYTES);
    auto b = roots.root(maxb.value());
    c.allocate_instance<IntObject>(at(CoreObject::IntType), maxs, BLAKE2S_OUTBYTES);
    auto s = roots.root(maxs.value());
    SignatureLayout unary;
    unary.parameters = {{U"self", ParameterKind::PositionalOnly}};
    auto pair = unary;
    pair.parameters.push_back({U"data", ParameterKind::PositionalOnly});
    auto xof = unary;
    xof.parameters.push_back({U"length", ParameterKind::PositionalOnly});
    for (auto kind : {CoreObject::HashType, CoreObject::HashXofType, CoreObject::Blake2bType,
                      CoreObject::Blake2sType}) {
        auto owner = at(kind);
        install_native_attribute(c, core, owner, U"update", pair, update);
        install_native_attribute(c, core, owner, U"copy", unary, copy);
        const auto &sig = kind == CoreObject::HashXofType ? xof : unary;
        install_native_attribute(c, core, owner, U"digest", sig, digest<false>);
        install_native_attribute(c, core, owner, U"hexdigest", sig, digest<true>);
        install_native_field(c, core, owner, U"name", get_name);
        install_native_field(c, core, owner, U"digest_size", get_size<false>);
        install_native_field(c, core, owner, U"block_size", get_size<true>);
    }
    for (bool small : {false, true}) {
        auto owner = at(small ? CoreObject::Blake2sType : CoreObject::Blake2bType);
        SignatureLayout sig;
        sig.parameters = {
            {U"cls", ParameterKind::PositionalOnly},
            {U"data", ParameterKind::PositionalOnly, e.get(c)},
            {U"digest_size", ParameterKind::KeywordOnly, (small ? s : b).get(c)},
            {U"key", ParameterKind::KeywordOnly, e.get(c)},
            {U"salt", ParameterKind::KeywordOnly, e.get(c)},
            {U"person", ParameterKind::KeywordOnly, e.get(c)},
            {U"fanout", ParameterKind::KeywordOnly, o.get(c)},
            {U"depth", ParameterKind::KeywordOnly, o.get(c)},
            {U"leaf_size", ParameterKind::KeywordOnly, z.get(c)},
            {U"node_offset", ParameterKind::KeywordOnly, z.get(c)},
            {U"node_depth", ParameterKind::KeywordOnly, z.get(c)},
            {U"inner_size", ParameterKind::KeywordOnly, z.get(c)},
            {U"last_node", ParameterKind::KeywordOnly, at(CoreObject::False).get(c)},
            {U"usedforsecurity", ParameterKind::KeywordOnly, at(CoreObject::True).get(c)}};
        install_native_attribute(c, core, owner, U"__new__", std::move(sig),
                                 small ? blake_type_new<true> : blake_type_new<false>);
        auto dictionary = *TypeView(c, owner).dictionary();
        auto function = roots.root(*name_lookup(c, dictionary, U"__new__"));
        c.allocate_instance<StaticMethodObject>(at(CoreObject::StaticMethodType), wrapper,
                                                function.get(c));
        c.allocate_instance<StrObject>(at(CoreObject::StrType), key, U"__new__");
        name_store(c, dictionary, key.value(), wrapper.value());
        for (const auto &[name, value] :
             {std::pair{U"MAX_DIGEST_SIZE", small ? static_cast<int>(BLAKE2S_OUTBYTES)
                                                  : static_cast<int>(BLAKE2B_OUTBYTES)},
              {U"MAX_KEY_SIZE",
               small ? static_cast<int>(BLAKE2S_KEYBYTES) : static_cast<int>(BLAKE2B_KEYBYTES)},
              {U"SALT_SIZE",
               small ? static_cast<int>(BLAKE2S_SALTBYTES) : static_cast<int>(BLAKE2B_SALTBYTES)},
              {U"PERSON_SIZE", small ? static_cast<int>(BLAKE2S_PERSONALBYTES)
                                     : static_cast<int>(BLAKE2B_PERSONALBYTES)}}) {
            c.allocate_instance<StrObject>(at(CoreObject::StrType), key, name);
            c.allocate_instance<IntObject>(at(CoreObject::IntType), constant, value);
            name_store(c, dictionary, key.value(), constant.value());
        }
    }
}

void initialize_hashlib(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    ReturnSlot empty(c), function(c), key(c), names(c);
    make_bytes(c, {}, empty);
    auto empty_value = roots.root(empty.value());
    const auto yes = c.builtin(CoreObject::True);
    auto install = [&](const char *name, SignatureLayout sig, NativeEntry entry) {
        make_native_function(c, name, std::move(sig), entry, {}, function,
                             NativeFunctionBinding::Function);
        make_str(c, std::u32string(name, name + std::strlen(name)), key);
        dict_store(c, dict, roots.root(key.value()), roots.root(function.value()), out);
        return out.tag() != ResultTag::Raised;
    };
    SignatureLayout ctor;
    ctor.parameters = {{U"data", ParameterKind::PositionalOnly, empty_value.get(c)},
                       {U"usedforsecurity", ParameterKind::KeywordOnly, yes}};
    const std::array<const char *, 12> labels = {
        "openssl_md5",      "openssl_sha1",     "openssl_sha224",    "openssl_sha256",
        "openssl_sha384",   "openssl_sha512",   "openssl_sha3_224",  "openssl_sha3_256",
        "openssl_sha3_384", "openssl_sha3_512", "openssl_shake_128", "openssl_shake_256"};
    const std::array<NativeEntry, 12> entries = {constructor<0>, constructor<1>,  constructor<2>,
                                                 constructor<3>, constructor<4>,  constructor<5>,
                                                 constructor<6>, constructor<7>,  constructor<8>,
                                                 constructor<9>, constructor<10>, constructor<11>};
    for (std::size_t i = 0; i < labels.size(); ++i)
        if (!install(labels[i], ctor, entries[i]))
            return;
    SignatureLayout generic;
    generic.parameters = {{U"name", ParameterKind::PositionalOrKeyword},
                          {U"data", ParameterKind::PositionalOrKeyword, empty_value.get(c)},
                          {U"usedforsecurity", ParameterKind::KeywordOnly, yes}};
    if (!install("new", generic, new_hash))
        return;
    SignatureLayout pb;
    pb.parameters = {{U"hash_name", ParameterKind::PositionalOrKeyword},
                     {U"password", ParameterKind::PositionalOrKeyword},
                     {U"salt", ParameterKind::PositionalOrKeyword},
                     {U"iterations", ParameterKind::PositionalOrKeyword},
                     {U"dklen", ParameterKind::PositionalOrKeyword, c.builtin(CoreObject::None)}};
    if (!install("pbkdf2_hmac", pb, pbkdf2))
        return;
    ReturnSlot zero(c), default_length(c);
    make_int(c, 0, zero);
    auto z = roots.root(zero.value());
    make_int(c, 64, default_length);
    auto length = roots.root(default_length.value());
    SignatureLayout sc;
    sc.parameters = {{U"password", ParameterKind::PositionalOnly},
                     {U"salt", ParameterKind::KeywordOnly},
                     {U"n", ParameterKind::KeywordOnly},
                     {U"r", ParameterKind::KeywordOnly},
                     {U"p", ParameterKind::KeywordOnly},
                     {U"maxmem", ParameterKind::KeywordOnly, z.get(c)},
                     {U"dklen", ParameterKind::KeywordOnly, length.get(c)}};
    if (!install("scrypt", sc, scrypt))
        return;
    make_set(c, {}, names);
    auto set = roots.root(names.value());
    for (const char *name : {"md5", "sha1", "sha224", "sha256", "sha384", "sha512", "sha3_224",
                             "sha3_256", "sha3_384", "sha3_512", "shake_128", "shake_256"}) {
        make_str(c, std::u32string(name, name + std::strlen(name)), key);
        set_add(c, set, roots.root(key.value()), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    make_str(c, U"openssl_md_meth_names", key);
    dict_store(c, dict, roots.root(key.value()), set, out);
    if (out.tag() != ResultTag::Raised)
        out.ok(c.builtin(CoreObject::None));
}

void initialize_blake2(Attachment &c, Handle module, ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    ReturnSlot key(c);
    for (const auto &[name, kind] :
         {std::pair{U"blake2b", CoreObject::Blake2bType}, {U"blake2s", CoreObject::Blake2sType}}) {
        make_str(c, name, key);
        dict_store(c, dict, roots.root(key.value()), roots.root(c.builtin(kind)), out);
        if (out.tag() == ResultTag::Raised)
            return;
    }
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
