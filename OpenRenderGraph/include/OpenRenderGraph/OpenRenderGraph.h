#pragma once

// Core user-facing API surface for OpenRenderGraph.
// Include this first for typical pass authoring and graph composition.

#include "../Render/RenderGraph/RenderGraph.h"
#include "../Render/PassBuilders.h"
#include "../Render/PassInputs.h"
#include "../Render/PassExecutionContext.h"
#include "../Render/QueueKind.h"
#include "../Render/DescriptorHeap.h"
#include "../Render/ImmediateExecution/ImmediateCommandList.h"
#include "../Render/CommandListPool.h"
#include "../Render/MemoryIntrospectionAPI.h"
#include "../Render/MemoryIntrospectionBackend.h"
#include "../Render/RenderGraph/Aliasing/RenderGraphAliasingSubsystem.h"

#include "../RenderPasses/Base/RenderPass.h"
#include "../RenderPasses/Base/ComputePass.h"
#include "../RenderPasses/Base/PassReturn.h"

#include "../Interfaces/IResourceProvider.h"
#include "../Interfaces/IResourceResolver.h"
#include "../Interfaces/IDynamicDeclaredResources.h"

#include "../Resources/ResourceIdentifier.h"
#include "../Resources/Resource.h"
#include "../Resources/TextureDescription.h"
#include "../Resources/Buffers/Buffer.h"
#include "../Resources/PixelBuffer.h"

#include "../Render/Runtime/IStatisticsService.h"
#include "../Render/Runtime/IUploadService.h"
#include "../Render/Runtime/IReadbackService.h"
#include "../Render/Runtime/IRenderGraphSettingsService.h"
#include "../Render/Runtime/UploadServiceAccess.h"
#include "../Render/Runtime/UploadTypes.h"
