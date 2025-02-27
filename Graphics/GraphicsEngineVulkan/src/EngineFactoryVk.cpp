/*     Copyright 2019 Diligent Graphics LLC
 *  
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 * 
 *     http://www.apache.org/licenses/LICENSE-2.0
 * 
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT OF ANY PROPRIETARY RIGHTS.
 *
 *  In no event and under no legal theory, whether in tort (including negligence), 
 *  contract, or otherwise, unless required by applicable law (such as deliberate 
 *  and grossly negligent acts) or agreed to in writing, shall any Contributor be
 *  liable for any damages, including any direct, indirect, special, incidental, 
 *  or consequential damages of any character arising as a result of this License or 
 *  out of the use or inability to use the software (including but not limited to damages 
 *  for loss of goodwill, work stoppage, computer failure or malfunction, or any and 
 *  all other commercial damages or losses), even if such Contributor has been advised 
 *  of the possibility of such damages.
 */

/// \file
/// Routines that initialize Vulkan-based engine implementation

#include "pch.h"
#include <array>
#include "EngineFactoryVk.h"
#include "RenderDeviceVkImpl.h"
#include "DeviceContextVkImpl.h"
#include "SwapChainVkImpl.h"
#include "EngineMemory.h"
#include "CommandQueueVkImpl.h"
#include "VulkanUtilities/VulkanInstance.h"
#include "VulkanUtilities/VulkanPhysicalDevice.h"
#include "EngineFactoryBase.h"

namespace Diligent
{

/// Engine factory for Vk implementation
class EngineFactoryVkImpl : public EngineFactoryBase<IEngineFactoryVk>
{
public:
    static EngineFactoryVkImpl* GetInstance()
    {
        static EngineFactoryVkImpl TheFactory;
        return &TheFactory;
    }

    using TBase = EngineFactoryBase<IEngineFactoryVk>;
    EngineFactoryVkImpl() :
        TBase{IID_EngineFactoryVk}
    {}

    void CreateDeviceAndContextsVk(const EngineVkCreateInfo& EngineCI, 
                                   IRenderDevice**           ppDevice, 
                                   IDeviceContext**          ppContexts)override final;

    void AttachToVulkanDevice(std::shared_ptr<VulkanUtilities::VulkanInstance>       Instance,
                              std::unique_ptr<VulkanUtilities::VulkanPhysicalDevice> PhysicalDevice,
                              std::shared_ptr<VulkanUtilities::VulkanLogicalDevice>  LogicalDevice,
                              size_t                    CommandQueueCount,
                              ICommandQueueVk**         ppCommandQueues,
                              const EngineVkCreateInfo& EngineCI, 
                              IRenderDevice**           ppDevice, 
                              IDeviceContext**          ppContexts);//override final;

    void CreateSwapChainVk(IRenderDevice*       pDevice, 
                           IDeviceContext*      pImmediateContext, 
                           const SwapChainDesc& SwapChainDesc, 
                           void*                pNativeWndHandle, 
                           ISwapChain**         ppSwapChain)override final;
};

/// Creates render device and device contexts for Vulkan backend

/// \param [in] EngineCI - Engine creation attributes.
/// \param [out] ppDevice - Address of the memory location where pointer to 
///                         the created device will be written
/// \param [out] ppContexts - Address of the memory location where pointers to 
///                           the contexts will be written. Immediate context goes at
///                           position 0. If EngineCI.NumDeferredContexts > 0,
///                           pointers to the deferred contexts are written afterwards.
void EngineFactoryVkImpl::CreateDeviceAndContextsVk(const EngineVkCreateInfo& _EngineCI,
                                                    IRenderDevice**           ppDevice, 
                                                    IDeviceContext**          ppContexts)
{
    VERIFY( ppDevice && ppContexts, "Null pointer provided" );
    if( !ppDevice || !ppContexts )
        return;

    EngineVkCreateInfo EngineCI = _EngineCI;

#if 0
    for (Uint32 Type = Vk_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; Type < Vk_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++Type)
    {
        auto CPUHeapAllocSize = EngineCI.CPUDescriptorHeapAllocationSize[Type];
        Uint32 MaxSize = 1 << 20;
        if (CPUHeapAllocSize > 1 << 20)
        {
            LOG_ERROR("CPU Heap allocation size is too large (", CPUHeapAllocSize, "). Max allowed size is ", MaxSize);
            return;
        }

        if ((CPUHeapAllocSize % 16) != 0)
        {
            LOG_ERROR("CPU Heap allocation size (", CPUHeapAllocSize, ") is expected to be multiple of 16");
            return;
        }
    }
#endif

    SetRawAllocator(EngineCI.pRawMemAllocator);

    *ppDevice = nullptr;
    memset(ppContexts, 0, sizeof(*ppContexts) * (1 + EngineCI.NumDeferredContexts));

    try
    {
        auto Instance = VulkanUtilities::VulkanInstance::Create(
            EngineCI.EnableValidation, 
            EngineCI.GlobalExtensionCount, 
            EngineCI.ppGlobalExtensionNames,
            reinterpret_cast<VkAllocationCallbacks*>(EngineCI.pVkAllocator));

        auto vkDevice = Instance->SelectPhysicalDevice();
        auto PhysicalDevice = VulkanUtilities::VulkanPhysicalDevice::Create(vkDevice);
        const auto& PhysicalDeviceFeatures = PhysicalDevice->GetFeatures();

        // If an implementation exposes any queue family that supports graphics operations, 
        // at least one queue family of at least one physical device exposed by the implementation 
        // must support both graphics and compute operations.

        VkDeviceQueueCreateInfo QueueInfo{};
        QueueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        QueueInfo.flags = 0; // reserved for future use
        // All commands that are allowed on a queue that supports transfer operations are also allowed on a 
        // queue that supports either graphics or compute operations.Thus, if the capabilities of a queue family 
        // include VK_QUEUE_GRAPHICS_BIT or VK_QUEUE_COMPUTE_BIT, then reporting the VK_QUEUE_TRANSFER_BIT 
        // capability separately for that queue family is optional (4.1).
        QueueInfo.queueFamilyIndex = PhysicalDevice->FindQueueFamily(VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT);
        QueueInfo.queueCount = 1;
        const float defaultQueuePriority = 1.0f; // Ask for highest priority for our queue. (range [0,1])
        QueueInfo.pQueuePriorities = &defaultQueuePriority;

        VkDeviceCreateInfo DeviceCreateInfo = {};
        DeviceCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
        DeviceCreateInfo.flags = 0; // Reserved for future use
        // https://www.khronos.org/registry/vulkan/specs/1.0/html/vkspec.html#extended-functionality-device-layer-deprecation
        DeviceCreateInfo.enabledLayerCount = 0; // Deprecated and ignored.
        DeviceCreateInfo.ppEnabledLayerNames = nullptr; // Deprecated and ignored
        DeviceCreateInfo.queueCreateInfoCount = 1;
        DeviceCreateInfo.pQueueCreateInfos = &QueueInfo;
        VkPhysicalDeviceFeatures DeviceFeatures = {};

#define ENABLE_FEATURE(Feature)\
        if (EngineCI.EnabledFeatures.Feature)\
        {                                                   \
            if (PhysicalDeviceFeatures.Feature)             \
                DeviceFeatures.Feature = VK_TRUE;           \
            else                                            \
            {                                               \
                LOG_WARNING_MESSAGE("Requested device feature " #Feature " is not supported by the physical device and will be disabled");\
                EngineCI.EnabledFeatures.Feature = false;   \
            }                                               \
        }

        ENABLE_FEATURE(depthBiasClamp)
        ENABLE_FEATURE(fillModeNonSolid)
        ENABLE_FEATURE(depthClamp)
        ENABLE_FEATURE(independentBlend)
        ENABLE_FEATURE(samplerAnisotropy)
        ENABLE_FEATURE(geometryShader)
        ENABLE_FEATURE(tessellationShader)
        ENABLE_FEATURE(dualSrcBlend)
        ENABLE_FEATURE(multiViewport)
        ENABLE_FEATURE(imageCubeArray)
        ENABLE_FEATURE(textureCompressionBC)
        ENABLE_FEATURE(vertexPipelineStoresAndAtomics)
        ENABLE_FEATURE(fragmentStoresAndAtomics)
        ENABLE_FEATURE(shaderStorageImageExtendedFormats)
#undef ENABLE_FEATURE

        DeviceCreateInfo.pEnabledFeatures = &DeviceFeatures; // NULL or a pointer to a VkPhysicalDeviceFeatures structure that contains 
                                                             // boolean indicators of all the features to be enabled.

        std::vector<const char*> DeviceExtensions = 
        { 
            VK_KHR_SWAPCHAIN_EXTENSION_NAME, 
            VK_KHR_MAINTENANCE1_EXTENSION_NAME // To allow negative viewport height
        };
        DeviceCreateInfo.ppEnabledExtensionNames = DeviceExtensions.empty() ? nullptr : DeviceExtensions.data();
        DeviceCreateInfo.enabledExtensionCount = static_cast<uint32_t>(DeviceExtensions.size());

        auto vkAllocator = Instance->GetVkAllocator();
        auto vkPhysicalDevice = PhysicalDevice->GetVkDeviceHandle();
        auto LogicalDevice = VulkanUtilities::VulkanLogicalDevice::Create(vkPhysicalDevice, DeviceCreateInfo, vkAllocator);

        RefCntAutoPtr<CommandQueueVkImpl> pCmdQueueVk;
        auto &RawMemAllocator = GetRawAllocator();
        pCmdQueueVk = NEW_RC_OBJ(RawMemAllocator, "CommandQueueVk instance", CommandQueueVkImpl)(LogicalDevice, QueueInfo.queueFamilyIndex);

        std::array<ICommandQueueVk*, 1> CommandQueues = {{pCmdQueueVk}};
        AttachToVulkanDevice(Instance, std::move(PhysicalDevice), LogicalDevice, CommandQueues.size(), CommandQueues.data(), EngineCI, ppDevice, ppContexts);

        FenceDesc Desc;
        Desc.Name = "Command queue fence";
        // Render device owns command queue that in turn owns the fence, so it is an internal device object
        bool IsDeviceInternal = true;
        auto* pRenderDeviceVk = ValidatedCast<RenderDeviceVkImpl>(*ppDevice);
        RefCntAutoPtr<FenceVkImpl> pFenceVk( NEW_RC_OBJ(RawMemAllocator, "FenceVkImpl instance", FenceVkImpl)(pRenderDeviceVk, Desc, IsDeviceInternal) );
        pCmdQueueVk->SetFence(std::move(pFenceVk));
    }
    catch(std::runtime_error& )
    {
        return;
    }
}

/// Attaches to existing Vulkan device

/// \param [in] Instance - shared pointer to a VulkanUtilities::VulkanInstance object
/// \param [in] PhysicalDevice - pointer to the object representing physical device
/// \param [in] LogicalDevice - shared pointer to a VulkanUtilities::VulkanLogicalDevice object
/// \param [in] pCommandQueue - pointer to the implementation of command queue
/// \param [in] EngineCI - Engine creation attributes.
/// \param [out] ppDevice - Address of the memory location where pointer to 
///                         the created device will be written
/// \param [out] ppContexts - Address of the memory location where pointers to 
///                           the contexts will be written. Immediate context goes at
///                           position 0. If EngineCI.NumDeferredContexts > 0,
///                           pointers to the deferred contexts are written afterwards.
void EngineFactoryVkImpl::AttachToVulkanDevice(std::shared_ptr<VulkanUtilities::VulkanInstance>       Instance,
                                               std::unique_ptr<VulkanUtilities::VulkanPhysicalDevice> PhysicalDevice,
                                               std::shared_ptr<VulkanUtilities::VulkanLogicalDevice>  LogicalDevice,
                                               size_t                      CommandQueueCount,
                                               ICommandQueueVk**           ppCommandQueues,
                                               const EngineVkCreateInfo&   EngineCI, 
                                               IRenderDevice**             ppDevice, 
                                               IDeviceContext**            ppContexts)
{
    VERIFY( ppCommandQueues && ppDevice && ppContexts, "Null pointer provided" );
    if(!LogicalDevice || !ppCommandQueues || !ppDevice || !ppContexts )
        return;

    *ppDevice = nullptr;
    memset(ppContexts, 0, sizeof(*ppContexts) * (1 + EngineCI.NumDeferredContexts));

    try
    {
        auto &RawMemAllocator = GetRawAllocator();
        RenderDeviceVkImpl *pRenderDeviceVk( NEW_RC_OBJ(RawMemAllocator, "RenderDeviceVkImpl instance", RenderDeviceVkImpl)(RawMemAllocator, this, EngineCI, CommandQueueCount, ppCommandQueues, Instance, std::move(PhysicalDevice), LogicalDevice) );
        pRenderDeviceVk->QueryInterface(IID_RenderDevice, reinterpret_cast<IObject**>(ppDevice) );

        std::shared_ptr<GenerateMipsVkHelper> GenerateMipsHelper(new GenerateMipsVkHelper(*pRenderDeviceVk));

        RefCntAutoPtr<DeviceContextVkImpl> pImmediateCtxVk( NEW_RC_OBJ(RawMemAllocator, "DeviceContextVkImpl instance", DeviceContextVkImpl)(pRenderDeviceVk, false, EngineCI, 0, 0, GenerateMipsHelper) );
        // We must call AddRef() (implicitly through QueryInterface()) because pRenderDeviceVk will
        // keep a weak reference to the context
        pImmediateCtxVk->QueryInterface(IID_DeviceContext, reinterpret_cast<IObject**>(ppContexts) );
        pRenderDeviceVk->SetImmediateContext(pImmediateCtxVk);

        for (Uint32 DeferredCtx = 0; DeferredCtx < EngineCI.NumDeferredContexts; ++DeferredCtx)
        {
            RefCntAutoPtr<DeviceContextVkImpl> pDeferredCtxVk( NEW_RC_OBJ(RawMemAllocator, "DeviceContextVkImpl instance", DeviceContextVkImpl)(pRenderDeviceVk, true, EngineCI, 1+DeferredCtx, 0, GenerateMipsHelper) );
            // We must call AddRef() (implicitly through QueryInterface()) because pRenderDeviceVk will
            // keep a weak reference to the context
            pDeferredCtxVk->QueryInterface(IID_DeviceContext, reinterpret_cast<IObject**>(ppContexts + 1 + DeferredCtx) );
            pRenderDeviceVk->SetDeferredContext(DeferredCtx, pDeferredCtxVk);
        }
    }
    catch( const std::runtime_error & )
    {
        if( *ppDevice )
        {
            (*ppDevice)->Release();
            *ppDevice = nullptr;
        }
        for(Uint32 ctx=0; ctx < 1 + EngineCI.NumDeferredContexts; ++ctx)
        {
            if( ppContexts[ctx] != nullptr )
            {
                ppContexts[ctx]->Release();
                ppContexts[ctx] = nullptr;
            }
        }

        LOG_ERROR( "Failed to create device and contexts" );
    }
}


/// Creates a swap chain for Direct3D12-based engine implementation

/// \param [in] pDevice - Pointer to the render device
/// \param [in] pImmediateContext - Pointer to the immediate device context
/// \param [in] SCDesc - Swap chain description
/// \param [in] pNativeWndHandle - Platform-specific native handle of the window 
///                                the swap chain will be associated with:
///                                * On Win32 platform, this should be window handle (HWND)
///                                * On Universal Windows Platform, this should be reference to the 
///                                  core window (Windows::UI::Core::CoreWindow)
///                                
/// \param [out] ppSwapChain    - Address of the memory location where pointer to the new 
///                               swap chain will be written
void EngineFactoryVkImpl::CreateSwapChainVk(IRenderDevice*       pDevice, 
                                            IDeviceContext*      pImmediateContext, 
                                            const SwapChainDesc& SCDesc, 
                                            void*                pNativeWndHandle, 
                                            ISwapChain**         ppSwapChain)
{
    VERIFY( ppSwapChain, "Null pointer provided" );
    if( !ppSwapChain )
        return;

    *ppSwapChain = nullptr;

    try
    {
        auto* pDeviceVk = ValidatedCast<RenderDeviceVkImpl>( pDevice );
        auto* pDeviceContextVk = ValidatedCast<DeviceContextVkImpl>(pImmediateContext);
        auto& RawMemAllocator = GetRawAllocator();

        if (pDeviceContextVk->GetSwapChain() != nullptr && SCDesc.IsPrimary)
        {
            LOG_ERROR_AND_THROW("Another swap chain labeled as primary has already been created. "
                                "There must only be one primary swap chain.");
        }


        auto* pSwapChainVk = NEW_RC_OBJ(RawMemAllocator, "SwapChainVkImpl instance", SwapChainVkImpl)(SCDesc, pDeviceVk, pDeviceContextVk, pNativeWndHandle);
        pSwapChainVk->QueryInterface( IID_SwapChain, reinterpret_cast<IObject**>(ppSwapChain) );

        if (SCDesc.IsPrimary)
        {
            pDeviceContextVk->SetSwapChain(pSwapChainVk);
            // Bind default render target
            pDeviceContextVk->SetRenderTargets( 0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
            // Set default viewport
            pDeviceContextVk->SetViewports( 1, nullptr, 0, 0 );
        
            auto NumDeferredCtx = pDeviceVk->GetNumDeferredContexts();
            for (size_t ctx = 0; ctx < NumDeferredCtx; ++ctx)
            {
                if (auto pDeferredCtx = pDeviceVk->GetDeferredContext(ctx))
                {
                    auto *pDeferredCtxVk = pDeferredCtx.RawPtr<DeviceContextVkImpl>();
                    pDeferredCtxVk->SetSwapChain(pSwapChainVk);
                    // We cannot bind default render target here because
                    // there is no guarantee that deferred context will be used
                    // in this frame. It is an error to bind 
                    // RTV of an inactive buffer in the swap chain
                }
            }
        }
    }
    catch( const std::runtime_error & )
    {
        if( *ppSwapChain )
        {
            (*ppSwapChain)->Release();
            *ppSwapChain = nullptr;
        }

        LOG_ERROR( "Failed to create the swap chain" );
    }
}


#ifdef DOXYGEN
/// Loads Direct3D12-based engine implementation and exports factory functions
/// \param [out] GetFactoryFunc - Pointer to the function that returns factory for Vk engine implementation.
///                               See EngineFactoryVkImpl.
/// \remarks Depending on the configuration and platform, the function loads different dll:
/// Platform\\Configuration   |           Debug               |        Release
/// --------------------------|-------------------------------|----------------------------
///         x86               | GraphicsEngineVk_32d.dll   |    GraphicsEngineVk_32r.dll
///         x64               | GraphicsEngineVk_64d.dll   |    GraphicsEngineVk_64r.dll
///
void LoadGraphicsEngineVk(GetEngineFactoryVkType &GetFactoryFunc)
{
    // This function is only required because DoxyGen refuses to generate documentation for a static function when SHOW_FILES==NO
    #error This function must never be compiled;    
}
#endif

API_QUALIFIER
IEngineFactoryVk* GetEngineFactoryVk()
{
    return EngineFactoryVkImpl::GetInstance();
}

}
