#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Page-level doubly-linked list LRU for cluster-LOD streaming.
//
// - Each page in the pool occupies exactly one node in the list (unless pinned).
// - Front = least-recently-used, back = most-recently-used.
// - Touch() moves a node to the back in O(1).
// - PopOldest() removes and returns the oldest page.
// - Pinned pages are tracked separately and never enter the LRU.
//
// Thread safety: none - intended to be owned by a single thread (the worker).
class CLodPageLRU {
public:
    CLodPageLRU() = default;
    ~CLodPageLRU();

    CLodPageLRU(const CLodPageLRU&) = delete;
    CLodPageLRU& operator=(const CLodPageLRU&) = delete;

    // Insert a page into the LRU as most-recently-used.
    // If already present, moves it to the MRU position.
    void Insert(uint32_t pageID);

    // Remove a page from the LRU entirely.
    // No-op if not present.
    void Remove(uint32_t pageID);

    // Move an existing page to the back (most-recently-used).
    // No-op if not present or pinned.
    void Touch(uint32_t pageID);

    // Remove and return the least-recently-used page.
    // Returns ~0u if the LRU is empty.
    uint32_t PopOldest();

    // Is this page tracked in the LRU?
    bool Contains(uint32_t pageID) const;

    // Number of pages in the LRU (excludes pinned).
    uint32_t Size() const { return static_cast<uint32_t>(m_map.size()); }

    // Clear all entries (LRU + pinned).
    void Clear();

    // Pinned page tracking (outside the LRU)
    void Pin(uint32_t pageID);
    void Unpin(uint32_t pageID);
    bool IsPinned(uint32_t pageID) const;
    uint32_t PinnedCount() const { return static_cast<uint32_t>(m_pinned.size()); }

private:
    struct Node {
        uint32_t pageID = 0;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    // Unlink a node from the list without destroying it.
    void Unlink(Node* node);

    // Append a node at the back of the list.
    void PushBack(Node* node);

    Node* m_head = nullptr;
    Node* m_tail = nullptr;

    // pageID -> Node* for O(1) lookup.
    std::unordered_map<uint32_t, Node*> m_map;

    // Pinned pages - always resident, never evictable.
    std::unordered_set<uint32_t> m_pinned;
};
