#pragma once

#include <cstdint>
#include <unordered_map>
#include <unordered_set>

// Intrusive doubly-linked list LRU for cluster-LOD streaming.
//
// - Each resident group occupies exactly one node in the list.
// - Front = least-recently-used, back = most-recently-used.
// - Touch() moves a node to the back in O(1).
// - EvictOldest() pops from the front, skipping pinned or invalid entries.
// - Pinned groups are tracked separately and never enter the LRU.
//
// Thread safety: none — intended to be owned by a single thread (the worker).
class CLodStreamingLRU {
public:
    CLodStreamingLRU() = default;
    ~CLodStreamingLRU();

    CLodStreamingLRU(const CLodStreamingLRU&) = delete;
    CLodStreamingLRU& operator=(const CLodStreamingLRU&) = delete;

    // Insert a group into the LRU as most-recently-used.
    // If already present, moves it to the MRU position.
    void Insert(uint32_t groupIndex);

    // Remove a group from the LRU entirely.
    // No-op if not present.
    void Remove(uint32_t groupIndex);

    // Move an existing group to the back (most-recently-used).
    // No-op if not present.
    void Touch(uint32_t groupIndex);

    // Pop and return the least-recently-used group index.
    // Skips any group for which `shouldSkip(groupIndex)` returns true
    // (those are re-inserted at the back).
    // Returns true if an eviction candidate was found, and sets outGroupIndex.
    // Returns false if the LRU is empty or all entries were skipped.
    template<typename SkipPredicate>
    bool EvictOldest(uint32_t& outGroupIndex, SkipPredicate shouldSkip);

    // Is this group tracked in the LRU?
    bool Contains(uint32_t groupIndex) const;

    // Number of groups in the LRU.
    uint32_t Size() const { return static_cast<uint32_t>(m_map.size()); }

    // Clear all entries.
    void Clear();

    // ── Pinned group tracking (outside the LRU) ───────────────────────
    void Pin(uint32_t groupIndex);
    void Unpin(uint32_t groupIndex);
    bool IsPinned(uint32_t groupIndex) const;
    uint32_t PinnedCount() const { return static_cast<uint32_t>(m_pinned.size()); }

private:
    struct Node {
        uint32_t groupIndex = 0;
        Node* prev = nullptr;
        Node* next = nullptr;
    };

    // Unlink a node from the list without destroying it.
    void Unlink(Node* node);

    // Append a node at the back of the list.
    void PushBack(Node* node);

    Node* m_head = nullptr;
    Node* m_tail = nullptr;

    // groupIndex → Node* for O(1) lookup.
    std::unordered_map<uint32_t, Node*> m_map;

    // Pinned groups — always resident, never evictable.
    std::unordered_set<uint32_t> m_pinned;
};

// ── Template implementation ────────────────────────────────────────────
template<typename SkipPredicate>
bool CLodStreamingLRU::EvictOldest(uint32_t& outGroupIndex, SkipPredicate shouldSkip) {
    // Walk from the front (oldest). Nodes that should be skipped are
    // moved to the back so they won't be re-scanned immediately.
    const uint32_t maxScans = Size();
    uint32_t scanned = 0;
    while (m_head != nullptr && scanned < maxScans) {
        Node* candidate = m_head;
        ++scanned;

        if (shouldSkip(candidate->groupIndex)) {
            // Move to back — this group is important right now.
            Unlink(candidate);
            PushBack(candidate);
            continue;
        }

        // Found a valid eviction candidate.
        outGroupIndex = candidate->groupIndex;
        Unlink(candidate);
        m_map.erase(candidate->groupIndex);
        delete candidate;
        return true;
    }

    return false;
}
