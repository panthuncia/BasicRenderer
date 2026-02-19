#pragma once

#include "Render/Runtime/IShaderService.h"

namespace rg::runtime {

inline IShaderService*& ShaderServiceSlot() {
    static IShaderService* service = nullptr;
    return service;
}

inline void SetActiveShaderService(IShaderService* service) {
    ShaderServiceSlot() = service;
}

inline IShaderService* GetActiveShaderService() {
    return ShaderServiceSlot();
}

}
