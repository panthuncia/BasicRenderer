#pragma once

#include <memory>
#include "Buffer.h"

struct BufferHandle {
    std::shared_ptr<Buffer> uploadBuffer = nullptr; // The upload buffer
    std::shared_ptr<Buffer> dataBuffer = nullptr; // The actual resource buffer
};