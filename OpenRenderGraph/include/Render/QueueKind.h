#pragma once

#include <cstdint>

enum class QueueKind : uint8_t { Graphics = 0, Compute = 1, Copy = 2, Count };