#include "Render/GraphExtensions/ClusterLOD/CLodPageLRU.h"

CLodPageLRU::~CLodPageLRU() {
    Clear();
}

void CLodPageLRU::Insert(uint32_t pageID) {
    auto it = m_map.find(pageID);
    if (it != m_map.end()) {
        // Already present - move to MRU position.
        Unlink(it->second);
        PushBack(it->second);
        return;
    }

    auto* node = new Node();
    node->pageID = pageID;
    m_map[pageID] = node;
    PushBack(node);
}

void CLodPageLRU::Remove(uint32_t pageID) {
    auto it = m_map.find(pageID);
    if (it == m_map.end()) return;

    Unlink(it->second);
    delete it->second;
    m_map.erase(it);
}

void CLodPageLRU::Touch(uint32_t pageID) {
    if (m_pinned.count(pageID)) return;
    auto it = m_map.find(pageID);
    if (it == m_map.end()) return;

    Unlink(it->second);
    PushBack(it->second);
}

uint32_t CLodPageLRU::PopOldest() {
    while (m_head) {
        Node* node = m_head;
        uint32_t pageID = node->pageID;
        Unlink(node);
        m_map.erase(pageID);
        delete node;

        if (m_pinned.count(pageID)) {
            // Pinned page was inconsistently in the LRU list — clean it
            // up by removing the node but do not return it for eviction.
            continue;
        }
        return pageID;
    }
    return ~0u;
}

bool CLodPageLRU::Contains(uint32_t pageID) const {
    return m_map.count(pageID) != 0;
}

void CLodPageLRU::Clear() {
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

// Pinned page tracking

void CLodPageLRU::Pin(uint32_t pageID) {
    // Remove from LRU if present - pinned pages are not evictable.
    Remove(pageID);
    m_pinned.insert(pageID);
}

void CLodPageLRU::Unpin(uint32_t pageID) {
    if (m_pinned.erase(pageID)) {
        Insert(pageID);
    }
}

bool CLodPageLRU::IsPinned(uint32_t pageID) const {
    return m_pinned.count(pageID) != 0;
}

// list helpers

void CLodPageLRU::Unlink(Node* node) {
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

void CLodPageLRU::PushBack(Node* node) {
    node->prev = m_tail;
    node->next = nullptr;

    if (m_tail) {
        m_tail->next = node;
    } else {
        m_head = node;
    }

    m_tail = node;
}
