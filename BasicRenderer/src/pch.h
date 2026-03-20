#pragma once

// Windows
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

// C++ Standard Library
#include <algorithm>
#include <array>
#include <cassert>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <span>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

// Third-party - spdlog (included in 84 TUs, ~12s build cost)
#include <spdlog/spdlog.h>

// DirectX
#include <DirectXMath.h>
