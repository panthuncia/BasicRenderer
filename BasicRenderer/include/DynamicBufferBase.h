#pragma once

#include <memory>

#include "RenderContext.h"
#include "ResourceStates.h"
#include "Resource.h"

class Buffer;

class DynamicBufferBase : public Resource {
public:
    DynamicBufferBase() {}
    std::shared_ptr<Buffer> m_uploadBuffer;
    std::shared_ptr<Buffer> m_dataBuffer;
protected:
    virtual void Transition(const RenderContext& context, ResourceState prevState, ResourceState newState) {};
};