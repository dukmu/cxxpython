#pragma once

#include "cxxpython/objects.hpp"

#include <boost/intrusive/set.hpp>

namespace cxxpython::core {
// The owning list and both indexes refer to this single stable native allocation.
// Snapshots copy only Entry, never the hooks or the owning-list iterator.
struct DictObject::Node {
    using Hook =
        boost::intrusive::set_member_hook<boost::intrusive::link_mode<boost::intrusive::safe_link>,
                                          boost::intrusive::optimize_size<true>>;
    Entry entry;
    // Borrowed immutable string object owned by entry.key, or null for unnamed keys.
    const std::u32string *const name;
    std::list<Node>::iterator order;
    Hook name_hook, hash_hook;
    Node(Entry entry, const std::u32string *name = nullptr) noexcept : entry(entry), name(name) {}
    Node(const Node &) = delete;
    Node &operator=(const Node &) = delete;
    struct NameKey {
        using type = std::u32string;
        const type &operator()(const Node &node) const noexcept
        {
            return *node.name;
        }
    };
    struct HashKey {
        using type = std::int64_t;
        const type &operator()(const Node &node) const noexcept
        {
            return node.entry.hash;
        }
    };
};

struct DictObject::Storage {
    using Index =
        boost::intrusive::set<Node,
                              boost::intrusive::member_hook<Node, Node::Hook, &Node::name_hook>,
                              boost::intrusive::key_of_value<Node::NameKey>>;
    using HashIndex = boost::intrusive::multiset<
        Node, boost::intrusive::member_hook<Node, Node::Hook, &Node::hash_hook>,
        boost::intrusive::key_of_value<Node::HashKey>>;
    std::mutex mutex;
    // Reverse destruction order detaches both indexes before freeing their nodes.
    std::list<Node> names;
    Index index;
    HashIndex hashes;
    std::uint64_t next_identity = 1, keys_version = 0;
    std::size_t external_bytes() const noexcept
    {
        return sizeof(Storage) + names.size() * (sizeof(Node) + 2 * sizeof(void *));
    }
};
} // namespace cxxpython::core
