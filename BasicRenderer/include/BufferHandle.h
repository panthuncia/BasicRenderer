#pragma once

#include <memory>
#include "Buffer.h"

struct BufferHandle {
    UINT index; // Index in the descriptor heap
    std::shared_ptr<Buffer> uploadBuffer; // The upload buffer
    std::shared_ptr<Buffer> dataBuffer; // The actual resource buffer
};