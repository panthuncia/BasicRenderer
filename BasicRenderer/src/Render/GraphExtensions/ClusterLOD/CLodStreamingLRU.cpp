#include "Render/GraphExtensions/ClusterLOD/CLodStreamingLRU.h"

#include <unordered_set>

CLodStreamingLRU::~CLodStreamingLRU() {
    Clear();
}

void CLodStreamingLRU::Insert(uint32_t groupIndex) {
    auto it = m_map.find(groupIndex);
    if (it != m_map.end()) {
        // Already present- move to MRU position.
        Unlink(it->second);
        PushBack(it->second);
        return;
    }

    auto* node = new Node();
    node->groupIndex = groupIndex;
    m_map[groupIndex] = node;
    PushBack(node);
}

void CLodStreamingLRU::Remove(uint32_t groupIndex) {
    auto it = m_map.find(groupIndex);
    if (it == m_map.end()) return;

    Unlink(it->second);
    delete it->second;
    m_map.erase(it);
}

void CLodStreamingLRU::Touch(uint32_t groupIndex) {
    auto it = m_map.find(groupIndex);
    if (it == m_map.end()) return;

    Unlink(it->second);
    PushBack(it->second);
}

bool CLodStreamingLRU::Contains(uint32_t groupIndex) const {
    return m_map.count(groupIndex) != 0;
}

void CLodStreamingLRU::Clear() {
    Node* cur = m_head;
    while (cur) {
        Node* next = cur->next;
        delete cur;
        cur = next;
    }
    m_head = nullptr;
    m_tail = nullptr;
    m_map.clear();
    m_pinned.clear();
}

// Pinned group tracking

void CLodStreamingLRU::Pin(uint32_t groupIndex) {
    // Remove from LRU if present- pinned groups are not evictable.
    Remove(groupIndex);
    m_pinned.insert(groupIndex);
}

void CLodStreamingLRU::Unpin(uint32_t groupIndex) {
    m_pinned.erase(groupIndex);
}

bool CLodStreamingLRU::IsPinned(uint32_t groupIndex) const {
    return m_pinned.count(groupIndex) != 0;
}

// list helpers

void CLodStreamingLRU::Unlink(Node* node) {
    if (node->prev) {
        node->prev->next = node->next;
    } else {
        m_head = node->next;
    }

    if (node->next) {
        node->next->prev = node->prev;
    } else {
        m_tail = node->prev;
    }

    node->prev = nullptr;
    node->next = nullptr;
}

void CLodStreamingLRU::PushBack(Node* node) {
    node->prev = m_tail;
    node->next = nullptr;

    if (m_tail) {
        m_tail->next = node;
    } else {
        m_head = node;
    }

    m_tail = node;
}
