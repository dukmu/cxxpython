#pragma once

#include "cxxpython/runtime.hpp"

#include <sys/socket.h>

namespace cxxpython::core::detail {
struct SocketAddress {
    sockaddr_storage storage{};
    socklen_t size = 0;
};
bool socket_address(Attachment &, int family, Value address, SocketAddress &, Handle gaierror,
                    ReturnSlot &, bool numeric = false);
void socket_address_value(Attachment &, const sockaddr *, socklen_t, ReturnSlot &);
void initialize_socket_addresses(Attachment &, Handle module, Handle gaierror, Handle herror,
                                 ReturnSlot &);
} // namespace cxxpython::core::detail
