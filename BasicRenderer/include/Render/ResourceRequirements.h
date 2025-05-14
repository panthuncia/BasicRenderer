#pragma once

class Resource;

struct ResourceRequirement {
    Resource*                   resource;
    RangeSpec                   range;
    ResourceAccessType          access;    // bitmask
    ResourceLayout              layout;
    ResourceSyncState           sync;
};