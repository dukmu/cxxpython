// Address handling informed by CPython 3.12.14 Modules/socketmodule.c (PSF license).
#include "internal/socket_addresses.hpp"

#include "cxxpython/import.hpp"
#include "cxxpython/memoryobject.hpp"
#include "cxxpython/protocol.hpp"
#include "internal/abstract.hpp"
#include "internal/compiler.hpp"
#include "internal/errors.hpp"
#include "internal/long.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <climits>
#include <cstring>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/un.h>
#include <unistd.h>

namespace cxxpython::core::detail {
namespace {
bool failed(ReturnSlot &out)
{
    return out.tag() == ResultTag::Raised;
}

void native_error(Attachment &c, Handle type, int code, const char *message, ReturnSlot &out)
{
    RootScope roots(c);
    ReturnSlot number(c), text(c), result(c);
    make_int(c, code, number);
    if (failed(number)) {
        out.raised(number.exception());
        return;
    }
    make_str(c, decode_filename(SourceBuffer("", message)), text);
    if (failed(text)) {
        out.raised(text.exception());
        return;
    }
    const Value args[] = {number.value(), text.value()};
    call(c, type, {args, {}}, result);
    out.raised(failed(result) ? result.exception() : result.value());
}
void gai_error(Attachment &c, Handle type, int code, ReturnSlot &out)
{
    native_error(c, type, code, gai_strerror(code), out);
}
bool native_int(Attachment &c, Value value, int &n, ReturnSlot &out)
{
    std::int64_t converted;
    if (!index_as_int64(c, value, converted, out))
        return false;
    if (converted < INT_MIN || converted > INT_MAX) {
        raise_error(c, CoreObject::OverflowErrorType, U"Python int too large to convert to C int",
                    out);
        return false;
    }
    n = static_cast<int>(converted);
    return true;
}
bool string_value(Attachment &c, Value value, std::string &s, ReturnSlot &out, bool bytes = true)
{
    if (c.has_layout<StrObject>(value))
        s = source_utf8(c.payload<StrObject>(value).value);
    else if (bytes && c.has_layout<BytesObject>(value))
        s = c.payload<BytesObject>(value).value;
    else {
        raise_error(c, CoreObject::TypeErrorType, U"expected str or bytes", out);
        return false;
    }
    if (s.find('\0') != std::string::npos) {
        raise_error(c, CoreObject::ValueErrorType, U"embedded null character", out);
        return false;
    }
    return true;
}
Handle capture(Attachment &c, Value function, std::size_t i, RootScope &roots)
{
    return roots.root(c.payload<NativeFunctionObject>(function).captures[i]);
}
bool bounded(Attachment &c, Value value, std::int64_t max, int &n, ReturnSlot &out)
{
    std::int64_t x;
    if (!index_as_int64(c, value, x, out))
        return false;
    if (x < 0 || x > max) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return false;
    }
    n = static_cast<int>(x);
    return true;
}
template <bool wide, bool host>
void byteorder(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    std::int64_t n;
    if constexpr (wide) {
        if (!c.has_layout<IntObject>(a[0])) {
            raise_error(c, CoreObject::TypeErrorType, U"expected int", out);
            return;
        }
    }
    if (!index_as_int64(c, a[0], n, out))
        return;
    if (n < 0 || n > (wide ? 0xffffffffLL : 0xffff)) {
        raise_error(c, CoreObject::OverflowErrorType, U"integer out of range", out);
        return;
    }
    if constexpr (wide)
        make_int(c,
                 host ? htonl(static_cast<std::uint32_t>(n)) : ntohl(static_cast<std::uint32_t>(n)),
                 out);
    else
        make_int(c,
                 host ? htons(static_cast<std::uint16_t>(n)) : ntohs(static_cast<std::uint16_t>(n)),
                 out);
}
void hostname(Attachment &c, Value, std::span<const Value>, ReturnSlot &out)
{
    char name[1025]{};
    int rc, error;
    {
        NativeWait wait(c);
        rc = ::gethostname(name, sizeof(name) - 1);
        error = rc < 0 ? errno : 0;
    }
    if (error)
        raise_os_error(c, error, out);
    else
        make_str(c, decode_filename(SourceBuffer("", name)), out);
}
} // namespace

bool socket_address(Attachment &c, int family, Value address, SocketAddress &result,
                    Handle gaierror, ReturnSlot &out, bool numeric)
{
    RootScope roots(c);
    auto object = roots.root(address);
    if (family == AF_UNIX) {
        std::string path;
        if (c.has_layout<BytesObject>(object.get(c)))
            path = c.payload<BytesObject>(object).value;
        else if (c.has_layout<StrObject>(object.get(c)))
            path = source_utf8(c.payload<StrObject>(object).value);
        else {
            raise_error(c, CoreObject::TypeErrorType, U"AF_UNIX path must be str or bytes", out);
            return false;
        }
        sockaddr_un un{};
        un.sun_family = AF_UNIX;
        const bool abstract = !path.empty() && path[0] == '\0';
        if ((!abstract && path.find('\0') != std::string::npos) ||
            path.size() >= sizeof(un.sun_path)) {
            raise_error(c, CoreObject::ValueErrorType, U"invalid AF_UNIX path", out);
            return false;
        }
        std::memcpy(un.sun_path, path.data(), path.size());
        result.size = static_cast<socklen_t>(offsetof(sockaddr_un, sun_path) + path.size() +
                                             (abstract ? 0 : 1));
        std::memcpy(&result.storage, &un, sizeof(un));
        return true;
    }
    if (family != AF_INET && family != AF_INET6) {
        raise_error(c, CoreObject::OSErrorType, U"address family not supported", out);
        return false;
    }
    if (!c.has_layout<TupleObject>(object.get(c))) {
        raise_error(c, CoreObject::TypeErrorType, U"address must be a tuple", out);
        return false;
    }
    const auto items = c.payload<TupleObject>(object).items;
    for (auto item : items)
        roots.root(item);
    if (items.size() < 2 || items.size() > (family == AF_INET ? 2U : 4U)) {
        raise_error(c, CoreObject::TypeErrorType, U"invalid address tuple", out);
        return false;
    }
    std::string host;
    if (!string_value(c, items[0], host, out))
        return false;
    int port, flow = 0;
    std::int64_t scope = 0;
    if (!bounded(c, items[1], 65535, port, out))
        return false;
    if (items.size() > 2 && !bounded(c, items[2], 0xfffff, flow, out))
        return false;
    if (items.size() > 3) {
        if (!index_as_int64(c, items[3], scope, out))
            return false;
        if (scope < 0 || scope > 0xffffffffLL) {
            raise_error(c, CoreObject::OverflowErrorType, U"scopeid out of range", out);
            return false;
        }
    }
    if (family == AF_INET && host == "<broadcast>")
        host = "255.255.255.255";
    addrinfo hints{};
    hints.ai_family = family;
    hints.ai_socktype = SOCK_DGRAM;
    if (host.empty())
        hints.ai_flags = AI_PASSIVE;
    if (numeric)
        hints.ai_flags |= AI_NUMERICHOST;
    addrinfo *resolved = nullptr;
    int error;
    const std::string service = std::to_string(port);
    {
        NativeWait wait(c);
        error = ::getaddrinfo(host.empty() ? nullptr : host.c_str(), service.c_str(), &hints,
                              &resolved);
    }
    if (error) {
        gai_error(c, gaierror, error, out);
        return false;
    }
    std::memcpy(&result.storage, resolved->ai_addr, resolved->ai_addrlen);
    result.size = static_cast<socklen_t>(resolved->ai_addrlen);
    ::freeaddrinfo(resolved);
    if (family == AF_INET6) {
        auto &v6 = reinterpret_cast<sockaddr_in6 &>(result.storage);
        v6.sin6_flowinfo = htonl(static_cast<std::uint32_t>(flow));
        if (items.size() > 3)
            v6.sin6_scope_id = static_cast<std::uint32_t>(scope);
    }
    return true;
}

void socket_address_value(Attachment &c, const sockaddr *address, socklen_t size, ReturnSlot &out)
{
    RootScope roots(c);
    if (address->sa_family == AF_UNIX) {
        const auto *un = reinterpret_cast<const sockaddr_un *>(address);
        auto n =
            size > offsetof(sockaddr_un, sun_path) ? size - offsetof(sockaddr_un, sun_path) : 0;
        if (n && un->sun_path[0] == '\0')
            make_bytes(c, std::string(un->sun_path, n), out);
        else
            make_str(c,
                     decode_filename(
                         SourceBuffer("", std::string(un->sun_path, strnlen(un->sun_path, n)))),
                     out);
        return;
    }
    if (address->sa_family != AF_INET && address->sa_family != AF_INET6) {
        raise_error(c, CoreObject::OSErrorType, U"address family not supported", out);
        return;
    }
    char host[INET6_ADDRSTRLEN]{};
    int port;
    std::uint32_t flow = 0, scope = 0;
    const void *src;
    if (address->sa_family == AF_INET) {
        const auto *v4 = reinterpret_cast<const sockaddr_in *>(address);
        src = &v4->sin_addr;
        port = ntohs(v4->sin_port);
    } else {
        const auto *v6 = reinterpret_cast<const sockaddr_in6 *>(address);
        src = &v6->sin6_addr;
        port = ntohs(v6->sin6_port);
        flow = ntohl(v6->sin6_flowinfo);
        scope = v6->sin6_scope_id;
    }
    if (!inet_ntop(address->sa_family, src, host, sizeof(host))) {
        raise_os_error(c, errno, out);
        return;
    }
    ReturnSlot text(c), p(c), f(c), s(c);
    make_str(c, decode_filename(SourceBuffer("", host)), text);
    make_int(c, port, p);
    if (address->sa_family == AF_INET) {
        const Value a[] = {text.value(), p.value()};
        make_tuple(c, a, out);
    } else {
        make_int(c, flow, f);
        make_int(c, scope, s);
        const Value a[] = {text.value(), p.value(), f.value(), s.value()};
        make_tuple(c, a, out);
    }
}

namespace {
void inet_aton_entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    std::string text;
    if (!string_value(c, a[0], text, out, false))
        return;
    in_addr address{};
    if (!::inet_aton(text.c_str(), &address)) {
        raise_error(c, CoreObject::OSErrorType, U"illegal IP address string passed to inet_aton",
                    out);
        return;
    }
    make_bytes(c, std::string(reinterpret_cast<const char *>(&address), sizeof(address)), out);
}
void inet_ntoa_entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    std::string data;
    if (!buffer_bytes(c, roots.root(a[0]), data, out)) {
        if (!failed(out))
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    if (data.size() != sizeof(in_addr)) {
        raise_error(c, CoreObject::OSErrorType, U"packed IP wrong length for inet_ntoa", out);
        return;
    }
    in_addr address{};
    std::memcpy(&address, data.data(), sizeof(address));
    char text[INET_ADDRSTRLEN];
    if (!::inet_ntop(AF_INET, &address, text, sizeof(text))) {
        raise_os_error(c, errno, out);
        return;
    }
    make_str(c, decode_filename(SourceBuffer("", text)), out);
}
void inet_pton_entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    int family;
    std::string text;
    if (!native_int(c, a[0], family, out) || !string_value(c, a[1], text, out, false))
        return;
    unsigned char bytes[sizeof(in6_addr)]{};
    int rc = ::inet_pton(static_cast<int>(family), text.c_str(), bytes);
    if (rc < 0) {
        raise_os_error(c, errno, out);
        return;
    }
    if (!rc) {
        raise_error(c, CoreObject::OSErrorType, U"illegal IP address string passed to inet_pton",
                    out);
        return;
    }
    make_bytes(c, std::string(reinterpret_cast<char *>(bytes), family == AF_INET ? 4 : 16), out);
}
void inet_ntop_entry(Attachment &c, Value, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    int family;
    if (!native_int(c, a[0], family, out))
        return;
    if (family != AF_INET && family != AF_INET6) {
        const auto number = std::to_string(family);
        raise_error(c, CoreObject::ValueErrorType,
                    U"unknown address family " + std::u32string(number.begin(), number.end()), out);
        return;
    }
    std::string data;
    if (!buffer_bytes(c, roots.root(a[1]), data, out)) {
        if (!failed(out))
            raise_error(c, CoreObject::TypeErrorType, U"a bytes-like object is required", out);
        return;
    }
    if (data.size() != (family == AF_INET ? 4U : 16U)) {
        raise_error(c, CoreObject::ValueErrorType, U"invalid length of packed IP address string",
                    out);
        return;
    }
    char text[INET6_ADDRSTRLEN];
    if (!::inet_ntop(static_cast<int>(family), data.data(), text, sizeof(text))) {
        raise_os_error(c, errno, out);
        return;
    }
    make_str(c, decode_filename(SourceBuffer("", text)), out);
}

struct AddressInfo {
    int family, type, protocol;
    SocketAddress address;
    std::string canonical;
};
void getaddrinfo_entry(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto error_type = capture(c, function, 0, roots);
    std::string host, service;
    bool host_none = a[0] == c.builtin(CoreObject::None);
    bool service_none = a[1] == c.builtin(CoreObject::None);
    if (!host_none && !string_value(c, a[0], host, out))
        return;
    if (!service_none) {
        if (c.has_layout<IntObject>(a[1])) {
            std::int64_t port;
            if (!index_as_int64(c, a[1], port, out))
                return;
            service = std::to_string(port);
        } else if (!string_value(c, a[1], service, out))
            return;
    }
    int nums[4];
    for (std::size_t i = 0; i < 4; ++i)
        if (!native_int(c, a[i + 2], nums[i], out))
            return;
    addrinfo hints{};
    hints.ai_family = static_cast<int>(nums[0]);
    hints.ai_socktype = static_cast<int>(nums[1]);
    hints.ai_protocol = static_cast<int>(nums[2]);
    hints.ai_flags = static_cast<int>(nums[3]);
    std::vector<AddressInfo> entries;
    int error;
    {
        NativeWait wait(c);
        addrinfo *result = nullptr;
        error = ::getaddrinfo(host_none ? nullptr : host.c_str(),
                              service_none ? nullptr : service.c_str(), &hints, &result);
        std::unique_ptr<addrinfo, decltype(&::freeaddrinfo)> owned(result, ::freeaddrinfo);
        if (!error) {
            for (auto *p = result; p; p = p->ai_next) {
                if (p->ai_addrlen > sizeof(sockaddr_storage))
                    continue;
                AddressInfo item{p->ai_family,
                                 p->ai_socktype,
                                 p->ai_protocol,
                                 {},
                                 p->ai_canonname ? p->ai_canonname : ""};
                item.address.size = static_cast<socklen_t>(p->ai_addrlen);
                std::memcpy(&item.address.storage, p->ai_addr, p->ai_addrlen);
                entries.push_back(std::move(item));
            }
        }
    }
    if (error) {
        gai_error(c, error_type, error, out);
        return;
    }
    std::vector<Value> values;
    for (const auto &entry : entries) {
        ReturnSlot family(c), type(c), protocol(c), canonical(c), address(c), tuple(c);
        make_int(c, entry.family, family);
        make_int(c, entry.type, type);
        make_int(c, entry.protocol, protocol);
        make_str(c, decode_filename(SourceBuffer("", entry.canonical)), canonical);
        socket_address_value(c, reinterpret_cast<const sockaddr *>(&entry.address.storage),
                             entry.address.size, address);
        if (failed(address)) {
            out.raised(address.exception());
            return;
        }
        const Value items[] = {family.value(), type.value(), protocol.value(), canonical.value(),
                               address.value()};
        make_tuple(c, items, tuple);
        if (failed(tuple)) {
            out.raised(tuple.exception());
            return;
        }
        values.push_back(roots.root(tuple.value()).get(c));
    }
    make_list(c, values, out);
}
void getnameinfo_entry(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto error_type = capture(c, function, 0, roots);
    int flags;
    if (!native_int(c, a[1], flags, out))
        return;
    if (!c.has_layout<TupleObject>(a[0])) {
        raise_error(c, CoreObject::TypeErrorType, U"sockaddr must be a tuple", out);
        return;
    }
    const auto &items = c.payload<TupleObject>(a[0]).items;
    const auto size = items.size();
    bool ipv6 = size > 2;
    if (size >= 1 && c.has_layout<StrObject>(items[0]))
        ipv6 = ipv6 || c.payload<StrObject>(items[0]).value.find(U':') != std::u32string::npos;
    SocketAddress address;
    if (!socket_address(c, ipv6 ? AF_INET6 : AF_INET, a[0], address, error_type, out, true))
        return;
    char host[NI_MAXHOST]{}, service[NI_MAXSERV]{};
    int error;
    {
        NativeWait wait(c);
        error =
            ::getnameinfo(reinterpret_cast<const sockaddr *>(&address.storage), address.size, host,
                          sizeof(host), service, sizeof(service), static_cast<int>(flags));
    }
    if (error) {
        gai_error(c, error_type, error, out);
        return;
    }
    ReturnSlot h(c), s(c);
    make_str(c, decode_filename(SourceBuffer("", host)), h);
    make_str(c, decode_filename(SourceBuffer("", service)), s);
    const Value values[] = {h.value(), s.value()};
    make_tuple(c, values, out);
}
void gethostbyname_entry(Attachment &c, Value function, std::span<const Value> a, ReturnSlot &out)
{
    RootScope roots(c);
    auto error_type = capture(c, function, 0, roots);
    std::string host;
    if (!string_value(c, a[0], host, out))
        return;
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *result = nullptr;
    int error;
    {
        NativeWait wait(c);
        error = ::getaddrinfo(host.c_str(), nullptr, &hints, &result);
    }
    if (error) {
        gai_error(c, error_type, error, out);
        return;
    }
    char text[INET_ADDRSTRLEN];
    auto address = reinterpret_cast<sockaddr_in *>(result->ai_addr)->sin_addr;
    ::freeaddrinfo(result);
    if (!::inet_ntop(AF_INET, &address, text, sizeof(text))) {
        raise_os_error(c, errno, out);
        return;
    }
    make_str(c, decode_filename(SourceBuffer("", text)), out);
}
} // namespace

void initialize_socket_addresses(Attachment &c, Handle module, Handle gaierror, Handle herror,
                                 ReturnSlot &out)
{
    RootScope roots(c);
    auto dict = roots.root(module_dict(c, module.get(c)));
    ReturnSlot name(c), function(c), zero(c);
    make_int(c, 0, zero);
    auto install = [&](const char *key, NativeEntry entry,
                       std::initializer_list<Parameter> params) {
        SignatureLayout sig;
        sig.parameters = params;
        const Value captures[] = {gaierror.get(c), herror.get(c)};
        make_native_function(c, key, std::move(sig), entry, captures, function,
                             NativeFunctionBinding::Function);
        if (failed(function)) {
            out.raised(function.exception());
            return false;
        }
        make_str(c, std::u32string(key, key + std::strlen(key)), name);
        if (failed(name)) {
            out.raised(name.exception());
            return false;
        }
        dict_store(c, dict, roots.root(name.value()), roots.root(function.value()), out);
        return !failed(out);
    };
    const auto p = ParameterKind::PositionalOnly;
    const auto z = zero.value();
    if (!install("gethostname", hostname, {}) ||
        !install("htons", byteorder<false, true>, {{U"x", p}}) ||
        !install("ntohs", byteorder<false, false>, {{U"x", p}}) ||
        !install("htonl", byteorder<true, true>, {{U"x", p}}) ||
        !install("ntohl", byteorder<true, false>, {{U"x", p}}) ||
        !install("inet_aton", inet_aton_entry, {{U"address", p}}) ||
        !install("inet_ntoa", inet_ntoa_entry, {{U"packed_ip", p}}) ||
        !install("inet_pton", inet_pton_entry, {{U"family", p}, {U"address", p}}) ||
        !install("inet_ntop", inet_ntop_entry, {{U"family", p}, {U"packed_ip", p}}) ||
        !install("gethostbyname", gethostbyname_entry, {{U"host", p}}) ||
        !install("getnameinfo", getnameinfo_entry, {{U"sockaddr", p}, {U"flags", p}}) ||
        !install("getaddrinfo", getaddrinfo_entry,
                 {{U"host", p},
                  {U"port", p},
                  {U"family", p, z},
                  {U"type", p, z},
                  {U"proto", p, z},
                  {U"flags", p, z}}))
        return;
    out.ok(c.builtin(CoreObject::None));
}
} // namespace cxxpython::core::detail
