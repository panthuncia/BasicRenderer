#pragma once

// bundle everything you need per-resource
struct ResourceRequirement {
    std::shared_ptr<Resource>   resource;
    ResourceAccessType          access;    // bitmask
    ResourceLayout              layout;
    ResourceSyncState           sync;
};