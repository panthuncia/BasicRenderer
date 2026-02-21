#include "Resources/Buffers/Buffer.h"

#include "OpenRenderGraph/Internal/Resources/GPUBacking/GpuBufferBacking.h"

void Buffer::OnSetName() {
    if (!m_dataBuffer) {
        return;
    }
    m_dataBuffer->SetName(name.c_str());
}
