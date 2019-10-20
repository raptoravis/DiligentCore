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

#include "pch.h"

#include "ShaderResourceCacheVk.h"
#include "DeviceContextVkImpl.h"
#include "BufferViewVkImpl.h"
#include "TextureViewVkImpl.h"
#include "TextureVkImpl.h"
#include "SamplerVkImpl.h"
#include "VulkanTypeConversions.h"

namespace Diligent
{

size_t ShaderResourceCacheVk::GetRequiredMemorySize(Uint32 NumSets, Uint32 SetSizes[])
{
    Uint32 TotalResources = 0;
    for (Uint32 t=0; t < NumSets; ++t)
        TotalResources += SetSizes[t];
    auto MemorySize = NumSets * sizeof(DescriptorSet) + TotalResources * sizeof(Resource);
    return MemorySize;
}

void ShaderResourceCacheVk::InitializeSets(IMemoryAllocator& MemAllocator, Uint32 NumSets, Uint32 SetSizes[])
{
    // Memory layout:
    //
    //  m_pMemory
    //  |
    //  V
    // ||  DescriptorSet[0]  |   ....    |  DescriptorSet[Ns-1]  |  Res[0]  |  ... |  Res[n-1]  |    ....     | Res[0]  |  ... |  Res[m-1]  ||
    //
    //
    //  Ns = m_NumSets

    VERIFY(m_pAllocator == nullptr && m_pMemory == nullptr, "Cache already initialized");
    m_pAllocator     = &MemAllocator;
    m_NumSets        = NumSets;
    m_TotalResources = 0;
    for (Uint32 t=0; t < NumSets; ++t)
        m_TotalResources += SetSizes[t];
    auto MemorySize = NumSets * sizeof(DescriptorSet) + m_TotalResources * sizeof(Resource);
    VERIFY_EXPR(MemorySize == GetRequiredMemorySize(NumSets, SetSizes));
#ifdef _DEBUG
    m_DbgInitializedResources.resize(m_NumSets);
#endif
    if (MemorySize > 0)
    {
        m_pMemory = ALLOCATE_RAW( *m_pAllocator, "Memory for shader resource cache data", MemorySize);
        auto* pSets = reinterpret_cast<DescriptorSet*>(m_pMemory);
        auto* pCurrResPtr = reinterpret_cast<Resource*>(pSets + m_NumSets);
        for (Uint32 t = 0; t < NumSets; ++t)
        {
            new(&GetDescriptorSet(t)) DescriptorSet(SetSizes[t], SetSizes[t] > 0 ? pCurrResPtr : nullptr);
            pCurrResPtr += SetSizes[t];
#ifdef _DEBUG
            m_DbgInitializedResources[t].resize(SetSizes[t]);
#endif
        }
        VERIFY_EXPR((char*)pCurrResPtr == (char*)m_pMemory + MemorySize);
    }
}

void ShaderResourceCacheVk::InitializeResources(Uint32 Set, Uint32 Offset, Uint32 ArraySize, SPIRVShaderResourceAttribs::ResourceType Type)
{
    auto& DescrSet = GetDescriptorSet(Set);
    for (Uint32 res = 0; res < ArraySize; ++res)
    {
        new(&DescrSet.GetResource(Offset + res)) Resource{Type};
#ifdef _DEBUG
        m_DbgInitializedResources[Set][Offset + res] = true;
#endif
    }
}

#ifdef _DEBUG
void ShaderResourceCacheVk::DbgVerifyResourceInitialization()const
{
    for (const auto& SetFlags : m_DbgInitializedResources)
    {
        for (auto ResInitialized : SetFlags)
            VERIFY(ResInitialized, "Not all resources in the cache have been initialized. This is a bug.");
    }
}
#endif

ShaderResourceCacheVk::~ShaderResourceCacheVk()
{
    if (m_pMemory)
    {
        auto* pResources = GetFirstResourcePtr();
        for (Uint32 res=0; res < m_TotalResources; ++res)
            pResources[res].~Resource();
        for (Uint32 t = 0; t < m_NumSets; ++t)
            GetDescriptorSet(t).~DescriptorSet();

        m_pAllocator->Free(m_pMemory);
    }
}

template<bool VerifyOnly>
void ShaderResourceCacheVk::TransitionResources(DeviceContextVkImpl* pCtxVkImpl)
{
    auto* pResources = GetFirstResourcePtr();
    for (Uint32 res = 0; res < m_TotalResources; ++res)
    {
        auto& Res = pResources[res];
        switch (Res.Type)
        {
            case SPIRVShaderResourceAttribs::ResourceType::UniformBuffer:
            {
                auto* pBufferVk = Res.pObject.RawPtr<BufferVkImpl>();
                if (pBufferVk != nullptr && pBufferVk->IsInKnownState())
                {
                    constexpr RESOURCE_STATE RequiredState = RESOURCE_STATE_CONSTANT_BUFFER;
                    VERIFY_EXPR((ResourceStateFlagsToVkAccessFlags(RequiredState) & VK_ACCESS_UNIFORM_READ_BIT) == VK_ACCESS_UNIFORM_READ_BIT);
                    const bool IsInRequiredState = pBufferVk->CheckState(RequiredState);
                    if (VerifyOnly)
                    {
                        if (!IsInRequiredState)
                        {
                            LOG_ERROR_MESSAGE("State of buffer '", pBufferVk->GetDesc().Name, "' is incorrect. Required state: ",
                                               GetResourceStateString(RequiredState),  ". Actual state: ", 
                                               GetResourceStateString(pBufferVk->GetState()), 
                                               ". Call IDeviceContext::TransitionShaderResources(), use RESOURCE_STATE_TRANSITION_MODE_TRANSITION "
                                               "when calling IDeviceContext::CommitShaderResources() or explicitly transition the buffer state "
                                               "with IDeviceContext::TransitionResourceStates().");
                        }
                    }
                    else
                    {
                        if (!IsInRequiredState)
                        {
                            pCtxVkImpl->TransitionBufferState(*pBufferVk, RESOURCE_STATE_UNKNOWN, RequiredState, true);
                        }
                        VERIFY_EXPR(pBufferVk->CheckAccessFlags(VK_ACCESS_UNIFORM_READ_BIT));
                    }
                }
            }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer:
            {
                auto* pBuffViewVk = Res.pObject.RawPtr<BufferViewVkImpl>();
                auto* pBufferVk = pBuffViewVk != nullptr ? ValidatedCast<BufferVkImpl>(pBuffViewVk->GetBuffer()) : nullptr;
                if (pBufferVk != nullptr && pBufferVk->IsInKnownState())
                {
                    const RESOURCE_STATE RequiredState = 
                        (Res.Type == SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer || 
                         Res.Type == SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer) ?
                        RESOURCE_STATE_UNORDERED_ACCESS : RESOURCE_STATE_SHADER_RESOURCE;
#ifdef _DEBUG
                    const VkAccessFlags  RequiredAccessFlags = (RequiredState == RESOURCE_STATE_SHADER_RESOURCE) ? 
                        VK_ACCESS_SHADER_READ_BIT : (VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
                    VERIFY_EXPR( (ResourceStateFlagsToVkAccessFlags(RequiredState) & RequiredAccessFlags) == RequiredAccessFlags);
#endif
                    const bool IsInRequiredState = pBufferVk->CheckState(RequiredState);

                    if (VerifyOnly)
                    {
                        if (!IsInRequiredState)
                        {
                            LOG_ERROR_MESSAGE("State of buffer '", pBufferVk->GetDesc().Name, "' is incorrect. Required state: ",
                                               GetResourceStateString(RequiredState),  ". Actual state: ", 
                                               GetResourceStateString(pBufferVk->GetState()), 
                                               ". Call IDeviceContext::TransitionShaderResources(), use RESOURCE_STATE_TRANSITION_MODE_TRANSITION "
                                               "when calling IDeviceContext::CommitShaderResources() or explicitly transition the buffer state "
                                               "with IDeviceContext::TransitionResourceStates().");
                        }
                    }
                    else
                    {
                        // When both old and new states are RESOURCE_STATE_UNORDERED_ACCESS, we need to execute UAV barrier
                        // to make sure that all UAV writes are complete and visible.
                        if (!IsInRequiredState || RequiredState == RESOURCE_STATE_UNORDERED_ACCESS)
                        {
                            pCtxVkImpl->TransitionBufferState(*pBufferVk, RESOURCE_STATE_UNKNOWN, RequiredState, true);
                        }
                        VERIFY_EXPR(pBufferVk->CheckAccessFlags(RequiredAccessFlags));
                    }
                }
            }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::SeparateImage:
            case SPIRVShaderResourceAttribs::ResourceType::SampledImage:
            case SPIRVShaderResourceAttribs::ResourceType::StorageImage:
            {
                auto* pTextureViewVk = Res.pObject.RawPtr<TextureViewVkImpl>();
                auto* pTextureVk = pTextureViewVk != nullptr ? ValidatedCast<TextureVkImpl>(pTextureViewVk->GetTexture()) : nullptr;
                if (pTextureVk != nullptr && pTextureVk->IsInKnownState())
                {
                    // The image subresources for a storage image must be in the VK_IMAGE_LAYOUT_GENERAL layout in 
                    // order to access its data in a shader (13.1.1)
                    // The image subresources for a sampled image or a combined image sampler must be in the 
                    // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 
                    // or VK_IMAGE_LAYOUT_GENERAL layout in order to access its data in a shader (13.1.3, 13.1.4).
                    RESOURCE_STATE RequiredState;
                    if (Res.Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage)
                    {
                        RequiredState = RESOURCE_STATE_UNORDERED_ACCESS;
                        VERIFY_EXPR(ResourceStateToVkImageLayout(RequiredState) == VK_IMAGE_LAYOUT_GENERAL);
                    }
                    else
                    {
                        if (pTextureVk->GetDesc().BindFlags & BIND_DEPTH_STENCIL)
                        {
                            // VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL must only be used as a read - only depth / stencil attachment 
                            // in a VkFramebuffer and/or as a read - only image in a shader (which can be read as a sampled image, combined 
                            // image / sampler and /or input attachment). This layout is valid only for image subresources of images created 
                            // with the VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT usage bit enabled. (11.4)
                            RequiredState = RESOURCE_STATE_DEPTH_READ;
                            VERIFY_EXPR(ResourceStateToVkImageLayout(RequiredState) == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL);
                        }
                        else
                        {
                            RequiredState = RESOURCE_STATE_SHADER_RESOURCE;
                            VERIFY_EXPR(ResourceStateToVkImageLayout(RequiredState) == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
                        }
                    }
                    const bool IsInRequiredState = pTextureVk->CheckState(RequiredState);

                    if (VerifyOnly)
                    {
                        if (!IsInRequiredState)
                        {
                            LOG_ERROR_MESSAGE("State of texture '", pTextureVk->GetDesc().Name, "' is incorrect. Required state: ",
                                              GetResourceStateString(RequiredState), ". Actual state: ", 
                                              GetResourceStateString(pTextureVk->GetState()), 
                                              ". Call IDeviceContext::TransitionShaderResources(), use RESOURCE_STATE_TRANSITION_MODE_TRANSITION "
                                               "when calling IDeviceContext::CommitShaderResources() or explicitly transition the texture state "
                                               "with IDeviceContext::TransitionResourceStates().");
                        }
                    }
                    else
                    {
                        // When both old and new states are RESOURCE_STATE_UNORDERED_ACCESS, we need to execute UAV barrier
                        // to make sure that all UAV writes are complete and visible.
                        if (!IsInRequiredState || RequiredState == RESOURCE_STATE_UNORDERED_ACCESS)
                        {
                            pCtxVkImpl->TransitionTextureState(*pTextureVk, RESOURCE_STATE_UNKNOWN, RequiredState, true);
                        }
                    }
                }
            }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::AtomicCounter:
            {
                // Nothing to do with atomic counters
            }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::SeparateSampler:
            {
                // Nothing to do with samplers
            }
            break;

            default: UNEXPECTED("Unexpected resource type");
        }
    }
}

template void ShaderResourceCacheVk::TransitionResources<false>(DeviceContextVkImpl* pCtxVkImpl);
template void ShaderResourceCacheVk::TransitionResources<true>(DeviceContextVkImpl* pCtxVkImpl);


VkDescriptorBufferInfo ShaderResourceCacheVk::Resource::GetUniformBufferDescriptorWriteInfo()const
{
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::UniformBuffer, "Uniform buffer resource is expected");
    DEV_CHECK_ERR(pObject != nullptr, "Unable to get uniform buffer write info: cached object is null");

    auto* pBuffVk = pObject.RawPtr<const BufferVkImpl>();
    // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER or VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC descriptor type require
    // buffer to be created with VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT
    VERIFY_EXPR( (pBuffVk->GetDesc().BindFlags & BIND_UNIFORM_BUFFER) != 0);

    VkDescriptorBufferInfo DescrBuffInfo;
    DescrBuffInfo.buffer = pBuffVk->GetVkBuffer();
    // If descriptorType is VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER or VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, the offset member 
    // of each element of pBufferInfo must be a multiple of VkPhysicalDeviceLimits::minUniformBufferOffsetAlignment (13.2.4)
    DescrBuffInfo.offset = 0;
    DescrBuffInfo.range  = pBuffVk->GetDesc().uiSizeInBytes;
    return DescrBuffInfo;
}

VkDescriptorBufferInfo ShaderResourceCacheVk::Resource::GetStorageBufferDescriptorWriteInfo()const
{
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer || 
           Type == SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer,
           "Storage buffer resource is expected");
    DEV_CHECK_ERR(pObject != nullptr, "Unable to get storage buffer write info: cached object is null");

    auto* pBuffViewVk = pObject.RawPtr<const BufferViewVkImpl>();
    const auto& ViewDesc = pBuffViewVk->GetDesc();
    auto* pBuffVk = pBuffViewVk->GetBufferVk();

    // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER or VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC descriptor type 
    // require buffer to be created with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT (13.2.4)
    if (Type == SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer)
    {
        // HLSL buffer SRVs are mapped to read-only storge buffers in SPIR-V
        VERIFY(ViewDesc.ViewType == BUFFER_VIEW_SHADER_RESOURCE, "Attempting to bind buffer view '", ViewDesc.Name, "' as read-only storage buffer. "
               "Expected view type is BUFFER_VIEW_SHADER_RESOURCE. Actual type: ", GetBufferViewTypeLiteralName(ViewDesc.ViewType));
        VERIFY((pBuffVk->GetDesc().BindFlags & BIND_SHADER_RESOURCE) != 0, "Buffer '", pBuffVk->GetDesc().Name, "' being set as read-only storage buffer was not created with BIND_SHADER_RESOURCE flag");
    }
    else if (Type == SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer)
    {
        VERIFY(ViewDesc.ViewType == BUFFER_VIEW_UNORDERED_ACCESS, "Attempting to bind buffer view '", ViewDesc.Name, "' as writable storage buffer. "
               "Expected view type is BUFFER_VIEW_UNORDERED_ACCESS. Actual type: ", GetBufferViewTypeLiteralName(ViewDesc.ViewType));
        VERIFY((pBuffVk->GetDesc().BindFlags & BIND_UNORDERED_ACCESS) != 0, "Buffer '", pBuffVk->GetDesc().Name, "' being set as writable storage buffer was not created with BIND_UNORDERED_ACCESS flag");
    }
    else
    {
        UNEXPECTED("Unexpected resource type");
    }

    VkDescriptorBufferInfo DescrBuffInfo;
    DescrBuffInfo.buffer = pBuffVk->GetVkBuffer();
    // If descriptorType is VK_DESCRIPTOR_TYPE_STORAGE_BUFFER or VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, the offset member
    // of each element of pBufferInfo must be a multiple of VkPhysicalDeviceLimits::minStorageBufferOffsetAlignment (13.2.4)
    DescrBuffInfo.offset = ViewDesc.ByteOffset;
    DescrBuffInfo.range  = ViewDesc.ByteWidth;
    return DescrBuffInfo;
}

VkDescriptorImageInfo ShaderResourceCacheVk::Resource::GetImageDescriptorWriteInfo(bool IsImmutableSampler)const
{
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage  ||
           Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage ||
           Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage,
           "Storage image, separate image or sampled image resource is expected");
    DEV_CHECK_ERR(pObject != nullptr, "Unable to get image descriptor write info: cached object is null");

    bool IsStorageImage = Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage;

    auto* pTexViewVk = pObject.RawPtr<const TextureViewVkImpl>();
    VERIFY_EXPR(pTexViewVk->GetDesc().ViewType == (IsStorageImage ? TEXTURE_VIEW_UNORDERED_ACCESS : TEXTURE_VIEW_SHADER_RESOURCE));

    VkDescriptorImageInfo DescrImgInfo;
    DescrImgInfo.sampler = VK_NULL_HANDLE;
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage || !IsImmutableSampler,
           "Immutable sampler can't be assigned to separarate image or storage image");
    if (Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage && !IsImmutableSampler)
    {
        // Immutable samplers are permanently bound into the set layout; later binding a sampler 
        // into an immutable sampler slot in a descriptor set is not allowed (13.2.1)
        auto* pSamplerVk = ValidatedCast<const SamplerVkImpl>(pTexViewVk->GetSampler());
        if (pSamplerVk != nullptr)
        {
            // If descriptorType is VK_DESCRIPTOR_TYPE_SAMPLER or VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 
            // and dstSet was not allocated with a layout that included immutable samplers for dstBinding with 
            // descriptorType, the sampler member of each element of pImageInfo must be a valid VkSampler 
            // object (13.2.4)
            DescrImgInfo.sampler = pSamplerVk->GetVkSampler();
        }
#ifdef DEVELOPMENT
        else
        {
            LOG_ERROR_MESSAGE("No sampler is assigned to texture view '", pTexViewVk->GetDesc().Name, "'");
        }
#endif
    }
    DescrImgInfo.imageView = pTexViewVk->GetVulkanImageView();
    
    // If descriptorType is VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, for each descriptor that will be accessed 
    // via load or store operations the imageLayout member for corresponding elements of pImageInfo 
    // MUST be VK_IMAGE_LAYOUT_GENERAL (13.2.4)
    if (IsStorageImage)
        DescrImgInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
    else
    {
        if (ValidatedCast<const TextureVkImpl>(pTexViewVk->GetTexture())->GetDesc().BindFlags & BIND_DEPTH_STENCIL)
            DescrImgInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
        else
            DescrImgInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }

    return DescrImgInfo;
}

VkBufferView ShaderResourceCacheVk::Resource::GetBufferViewWriteInfo()const
{
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer ||
           Type == SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer,
           "Uniform or storage buffer resource is expected");
    DEV_CHECK_ERR(pObject != nullptr, "Unable to get buffer view write info: cached object is null");

    // The following bits must have been set at buffer creation time:
    //  * VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER  ->  VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
    //  * VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER  ->  VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT
    auto* pBuffViewVk = pObject.RawPtr<const BufferViewVkImpl>();
    return pBuffViewVk->GetVkBufferView();
}

VkDescriptorImageInfo ShaderResourceCacheVk::Resource::GetSamplerDescriptorWriteInfo()const
{
    VERIFY(Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler, "Separate sampler resource is expected");
    DEV_CHECK_ERR(pObject != nullptr, "Unable to get separate sampler descriptor write info: cached object is null");

    auto* pSamplerVk = pObject.RawPtr<const SamplerVkImpl>();
    VkDescriptorImageInfo DescrImgInfo;
    // For VK_DESCRIPTOR_TYPE_SAMPLER, only the sample member of each element of VkWriteDescriptorSet::pImageInfo is accessed (13.2.4)
    DescrImgInfo.sampler     = pSamplerVk->GetVkSampler();
    DescrImgInfo.imageView   = VK_NULL_HANDLE;
    DescrImgInfo.imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    return DescrImgInfo;
}

}
