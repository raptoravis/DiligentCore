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

#include "ShaderResourceLayoutVk.h"
#include "ShaderResourceCacheVk.h"
#include "BufferVkImpl.h"
#include "BufferViewVk.h"
#include "TextureVkImpl.h"
#include "TextureViewVkImpl.h"
#include "SamplerVkImpl.h"
#include "ShaderVkImpl.h"
#include "PipelineLayout.h"
#include "ShaderResourceVariableBase.h"
#include "StringTools.h"
#include "PipelineStateVkImpl.h"

namespace Diligent
{
 
static Int32 FindImmutableSampler(SHADER_TYPE                       ShaderType,
                                  const PipelineResourceLayoutDesc& ResourceLayoutDesc,
                                  const SPIRVShaderResourceAttribs& Attribs,
                                  const char*                       SamplerSuffix)
{
    if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage)
    {
        SamplerSuffix = nullptr;
    }
    else if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler)
    {
        // Use SamplerSuffix. If HLSL-style combined images samplers are not used,
        // SamplerSuffix will be null and we will be looking for the sampler itself.
    }
    else
    {
        UNEXPECTED("Immutable sampler can only be assigned to a sampled image or separate sampler");
        return -1;
    }

    for (Uint32 s=0; s < ResourceLayoutDesc.NumStaticSamplers; ++s)
    {
        const auto& StSam = ResourceLayoutDesc.StaticSamplers[s];
        if ( ((StSam.ShaderStages & ShaderType) != 0) && StreqSuff(Attribs.Name, StSam.SamplerOrTextureName, SamplerSuffix) )
            return s;
    }

    return -1;
}

static SHADER_RESOURCE_VARIABLE_TYPE FindShaderVariableType(SHADER_TYPE                       ShaderType,
                                                            const SPIRVShaderResourceAttribs& Attribs,
                                                            const PipelineResourceLayoutDesc& ResourceLayoutDesc,
                                                            const char*                       CombinedSamplerSuffix)
{
    if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler)
    {
        // Use texture or sampler name to derive separate sampler type
        // When HLSL-style combined image samplers are not used, CombinedSamplerSuffix is null
        return GetShaderVariableType(ShaderType, ResourceLayoutDesc.DefaultVariableType, ResourceLayoutDesc.Variables, ResourceLayoutDesc.NumVariables,
                                     [&](const char* VarName)
                                     {
                                         return StreqSuff(Attribs.Name, VarName, CombinedSamplerSuffix);
                                     });
    }
    else
    {
        return GetShaderVariableType(ShaderType, Attribs.Name, ResourceLayoutDesc);
    }
}


ShaderResourceLayoutVk::~ShaderResourceLayoutVk()
{
    for (Uint32 r=0; r < GetTotalResourceCount(); ++r)
        GetResource(r).~VkResource();

    for(Uint32 s=0; s < m_NumImmutableSamplers; ++s)
        GetImmutableSampler(s).~ImmutableSamplerPtrType();
}

void ShaderResourceLayoutVk::AllocateMemory(std::shared_ptr<const SPIRVShaderResources>  pSrcResources, 
                                            IMemoryAllocator&                            Allocator,
                                            const PipelineResourceLayoutDesc&            ResourceLayoutDesc,
                                            const SHADER_RESOURCE_VARIABLE_TYPE*         AllowedVarTypes,
                                            Uint32                                       NumAllowedTypes,
                                            bool                                         AllocateImmutableSamplers)
{
    VERIFY(!m_ResourceBuffer, "Memory has already been initialized");
    VERIFY_EXPR(!m_pResources);
    VERIFY_EXPR(pSrcResources);

    m_pResources = std::move(pSrcResources);

    const Uint32 AllowedTypeBits       = GetAllowedTypeBits(AllowedVarTypes, NumAllowedTypes);
    const auto   ShaderType            = m_pResources->GetShaderType();
    const auto*  CombinedSamplerSuffix = m_pResources->GetCombinedSamplerSuffix();
    // Count the number of resources to allocate all needed memory
    m_pResources->ProcessResources(
        [&](const SPIRVShaderResourceAttribs& ResAttribs, Uint32)
        {
            auto VarType = FindShaderVariableType(ShaderType, ResAttribs, ResourceLayoutDesc, CombinedSamplerSuffix);
            if (IsAllowedType(VarType, AllowedTypeBits))
            {
                // For immutable separate samplers we still allocate VkResource instances, but they are never exposed to the app

                VERIFY( Uint32{m_NumResources[VarType]} + 1 <= Uint32{std::numeric_limits<Uint16>::max()}, "Number of resources exceeds Uint16 maximum representable value");
                ++m_NumResources[VarType];
            }
        }
    );

    Uint32 TotalResources = 0;
    for (SHADER_RESOURCE_VARIABLE_TYPE VarType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC; VarType < SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_RESOURCE_VARIABLE_TYPE>(VarType+1))
    {
        TotalResources += m_NumResources[VarType];
    }
    VERIFY(TotalResources <= Uint32{std::numeric_limits<Uint16>::max()}, "Total number of resources exceeds Uint16 maximum representable value" );
    m_NumResources[SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES] = static_cast<Uint16>(TotalResources);

    m_NumImmutableSamplers = 0;
    if (AllocateImmutableSamplers)
    {
        for (Uint32 s=0; s < ResourceLayoutDesc.NumStaticSamplers; ++s)
        {
            const auto& StSamDesc = ResourceLayoutDesc.StaticSamplers[s];
            if ((StSamDesc.ShaderStages & ShaderType) != 0)
                ++m_NumImmutableSamplers;
        }
    }

    size_t MemSize = TotalResources * sizeof(VkResource) + m_NumImmutableSamplers * sizeof(ImmutableSamplerPtrType);
    static_assert( (sizeof(VkResource) % sizeof(void*)) == 0, "sizeof(VkResource) must be multiple of sizeof(void*)" );
    if (MemSize == 0)
        return;

    auto* pRawMem = ALLOCATE_RAW(Allocator, "Raw memory buffer for shader resource layout resources", MemSize);
    m_ResourceBuffer = std::unique_ptr<void, STDDeleterRawMem<void> >(pRawMem, Allocator);
    for (Uint32 s=0; s < m_NumImmutableSamplers; ++s)
    {
        // We need to initialize immutable samplers
        auto& UninitializedImmutableSampler = GetImmutableSampler(s);
        new(std::addressof(UninitializedImmutableSampler)) ImmutableSamplerPtrType;
    }
}


void ShaderResourceLayoutVk::InitializeStaticResourceLayout(std::shared_ptr<const SPIRVShaderResources> pSrcResources,
                                                            IMemoryAllocator&                           LayoutDataAllocator,
                                                            const PipelineResourceLayoutDesc&           ResourceLayoutDesc,
                                                            ShaderResourceCacheVk&                      StaticResourceCache)
{
    const auto AllowedVarType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC;
    // We do not need immutable samplers in static shader resource layout as they
    // are relevant only when the main layout is initialized 
    constexpr bool AllocateImmutableSamplers = false;
    AllocateMemory(std::move(pSrcResources), LayoutDataAllocator, ResourceLayoutDesc, &AllowedVarType, 1, AllocateImmutableSamplers);

    std::array<Uint32, SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES> CurrResInd = {};
    Uint32 StaticResCacheSize = 0;

    const Uint32 AllowedTypeBits       = GetAllowedTypeBits(&AllowedVarType, 1);
    const auto   ShaderType            = m_pResources->GetShaderType();
    const auto*  CombinedSamplerSuffix = m_pResources->GetCombinedSamplerSuffix();

    m_pResources->ProcessResources(
        [&](const SPIRVShaderResourceAttribs& Attribs, Uint32)
        {
            auto VarType = FindShaderVariableType(ShaderType, Attribs, ResourceLayoutDesc, CombinedSamplerSuffix);
            if (!IsAllowedType(VarType, AllowedTypeBits))
                return;

            Int32 SrcImmutableSamplerInd = -1;
            if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage || 
                Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler)
            {
                // Only search for the immutable sampler for combined image samplers and separate samplers
                SrcImmutableSamplerInd = FindImmutableSampler(ShaderType, ResourceLayoutDesc, Attribs, CombinedSamplerSuffix);
                // For immutable separate samplers we allocate VkResource instances, but they are never exposed to the app
            }

            Uint32 Binding       = Attribs.Type;
            Uint32 DescriptorSet = 0;
            Uint32 CacheOffset   = StaticResCacheSize;
            StaticResCacheSize += Attribs.ArraySize;
            
            Uint32 SamplerInd = VkResource::InvalidSamplerInd;
            if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage)
            {
                // Separate samplers are enumerated before separate images, so the sampler
                // assigned to this separate image must have already been created.
                SamplerInd = FindAssignedSampler(Attribs, CurrResInd[VarType], VarType);
            }
            ::new (&GetResource(VarType, CurrResInd[VarType]++)) VkResource(*this, Attribs, VarType, Binding, DescriptorSet, CacheOffset, SamplerInd, SrcImmutableSamplerInd >= 0);
        }
    );

#ifdef _DEBUG
    for (SHADER_RESOURCE_VARIABLE_TYPE VarType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC; VarType < SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_RESOURCE_VARIABLE_TYPE>(VarType + 1))
    {
        VERIFY(CurrResInd[VarType] == m_NumResources[VarType], "Not all resources have been initialized, which will cause a crash when dtor is called");
    }
#endif

    StaticResourceCache.InitializeSets(GetRawAllocator(), 1, &StaticResCacheSize);
    InitializeResourceMemoryInCache(StaticResourceCache);
#ifdef _DEBUG
    StaticResourceCache.DbgVerifyResourceInitialization();
#endif
}

#ifdef DEVELOPMENT
void ShaderResourceLayoutVk::dvpVerifyResourceLayoutDesc(Uint32                                             NumShaders,
                                                         const std::shared_ptr<const SPIRVShaderResources>  pShaderResources[],
                                                         const PipelineResourceLayoutDesc&                  ResourceLayoutDesc)
{
    auto GetAllowedShadersString = [&](SHADER_TYPE ShaderStages)
    {
        std::string ShadersStr;
        for (Uint32 s=0; s < NumShaders; ++s)
        {
            const auto& Resources = *pShaderResources[s];
            if ((ShaderStages & Resources.GetShaderType()) != 0)
            {
                ShadersStr.append(ShadersStr.empty() ? "'" : ", '");
                ShadersStr.append(Resources.GetShaderName());
                ShadersStr.append("' (");
                ShadersStr.append(GetShaderTypeLiteralName(Resources.GetShaderType()));
                ShadersStr.push_back(')');
            }
        }
        return ShadersStr;
    };

    for (Uint32 v = 0; v < ResourceLayoutDesc.NumVariables; ++v)
    {
        const auto& VarDesc = ResourceLayoutDesc.Variables[v];
        if (VarDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
        {
            LOG_WARNING_MESSAGE("No allowed shader stages are specified for ", GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name, "'.");
            continue;
        }

        bool VariableFound = false;
        for (Uint32 s=0; s < NumShaders && !VariableFound; ++s)
        {
            const auto& Resources = *pShaderResources[s];
            if ( (VarDesc.ShaderStages & Resources.GetShaderType()) != 0 )
            {
                for (Uint32 res = 0; res < Resources.GetTotalResources() && !VariableFound; ++res)
                {
                    const auto& ResAttribs = Resources.GetResource(res);
                    VariableFound = (strcmp(ResAttribs.Name, VarDesc.Name) == 0);
                }
            }
        }
        if (!VariableFound)
        {
            LOG_WARNING_MESSAGE(GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name,
                                "' is not found in any of the designated shader stages: ",
                                GetAllowedShadersString(VarDesc.ShaderStages));
        }
    }

    for (Uint32 sam = 0; sam < ResourceLayoutDesc.NumStaticSamplers; ++sam)
    {
        const auto& StSamDesc = ResourceLayoutDesc.StaticSamplers[sam];
        if (StSamDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
        {
            LOG_WARNING_MESSAGE("No allowed shader stages are specified for static sampler '", StSamDesc.SamplerOrTextureName, "'.");
            continue;
        }

        bool SamplerFound = false;
        for (Uint32 s=0; s < NumShaders && !SamplerFound; ++s)
        {
            const auto& Resources = *pShaderResources[s];
            if ( (StSamDesc.ShaderStages & Resources.GetShaderType()) == 0 )
                continue;

            // Irrespective of whether HLSL-style combined image samplers are used,
            // a static sampler can be assigned to GLSL sampled image (i.e. sampler2D g_tex)
            for (Uint32 i = 0; i < Resources.GetNumSmpldImgs() && !SamplerFound; ++i)
            {
                const auto& SmplImg = Resources.GetSmpldImg(i);
                SamplerFound = (strcmp(SmplImg.Name, StSamDesc.SamplerOrTextureName) == 0);
            }

            if (!SamplerFound)
            {
                // Check if static sampler is assigned to a separate sampler. 
                // In case HLSL-style combined image samplers are used, the condition is  SepSmpl.Name == "g_Texture" + "_sampler".
                // Otherwise the condition is  SepSmpl.Name == "g_Texture_sampler" + "".
                const auto* CombinedSamplerSuffix = Resources.GetCombinedSamplerSuffix();
                for (Uint32 i = 0; i < Resources.GetNumSepSmplrs() && !SamplerFound; ++i)
                {
                    const auto& SepSmpl = Resources.GetSepSmplr(i);
                    SamplerFound = StreqSuff(SepSmpl.Name, StSamDesc.SamplerOrTextureName, CombinedSamplerSuffix);
                }
            }
        }

        if (!SamplerFound)
        {
            LOG_WARNING_MESSAGE("Static sampler '", StSamDesc.SamplerOrTextureName,
                                "' is not found in any of the designated shader stages: ",
                                GetAllowedShadersString(StSamDesc.ShaderStages));
        }
    }
}
#endif

void ShaderResourceLayoutVk::Initialize(IRenderDevice*                               pRenderDevice,
                                        Uint32                                       NumShaders,
                                        ShaderResourceLayoutVk                       Layouts[],
                                        std::shared_ptr<const SPIRVShaderResources>  pShaderResources[],
                                        IMemoryAllocator&                            LayoutDataAllocator,
                                        const PipelineResourceLayoutDesc&            ResourceLayoutDesc,
                                        std::vector<uint32_t>                        SPIRVs[],
                                        class PipelineLayout&                        PipelineLayout)
{
#ifdef DEVELOPMENT
    dvpVerifyResourceLayoutDesc(NumShaders, pShaderResources, ResourceLayoutDesc);
#endif

    const SHADER_RESOURCE_VARIABLE_TYPE* AllowedVarTypes = nullptr;
    const Uint32                         NumAllowedTypes = 0;
    const Uint32                         AllowedTypeBits = GetAllowedTypeBits(AllowedVarTypes, NumAllowedTypes);

    for (Uint32 s=0; s < NumShaders; ++s)
    {
        constexpr bool AllocateImmutableSamplers = true;
        Layouts[s].AllocateMemory(std::move(pShaderResources[s]), LayoutDataAllocator, ResourceLayoutDesc, AllowedVarTypes, NumAllowedTypes, AllocateImmutableSamplers);
    }
    
    VERIFY_EXPR(NumShaders <= MaxShadersInPipeline);
    std::array<std::array<Uint32, SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES>, MaxShadersInPipeline> CurrResInd = {};
    std::array<Uint32, MaxShadersInPipeline> CurrImmutableSamplerInd = {};
#ifdef _DEBUG
    std::unordered_map<Uint32, std::pair<Uint32, Uint32>> dbgBindings_CacheOffsets;
#endif

    auto AddResource = [&](Uint32                            ShaderInd,
                           ShaderResourceLayoutVk&           ResLayout,
                           const SPIRVShaderResources&       Resources, 
                           const SPIRVShaderResourceAttribs& Attribs)
    {
        const auto ShaderType = Resources.GetShaderType();
        const SHADER_RESOURCE_VARIABLE_TYPE VarType = FindShaderVariableType(ShaderType, Attribs, ResourceLayoutDesc, Resources.GetCombinedSamplerSuffix());
        if (!IsAllowedType(VarType, AllowedTypeBits))
            return;

        Uint32 Binding       = 0;
        Uint32 DescriptorSet = 0;
        Uint32 CacheOffset   = 0;
        Uint32 SamplerInd    = VkResource::InvalidSamplerInd;
        
        if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage)
        {
            // Separate samplers are enumerated before separate images, so the sampler
            // assigned to this separate image must have already been created.
            SamplerInd = ResLayout.FindAssignedSampler(Attribs, CurrResInd[ShaderInd][VarType], VarType);
        }

        VkSampler vkImmutableSampler = VK_NULL_HANDLE;
        if (Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage || 
            Attribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler)
        {
            // Only search for the immutable sampler for combined image samplers and separate samplers
            Int32 SrcImmutableSamplerInd = FindImmutableSampler(ShaderType, ResourceLayoutDesc, Attribs, Resources.GetCombinedSamplerSuffix());
            if (SrcImmutableSamplerInd >= 0)
            {
                auto& ImmutableSampler = ResLayout.GetImmutableSampler(CurrImmutableSamplerInd[ShaderInd]++);
                VERIFY(!ImmutableSampler, "Immutable sampler has already been initialized!");
                const auto& ImmutableSamplerDesc = ResourceLayoutDesc.StaticSamplers[SrcImmutableSamplerInd].Desc;
                pRenderDevice->CreateSampler(ImmutableSamplerDesc, &ImmutableSampler);
                vkImmutableSampler = ImmutableSampler.RawPtr<SamplerVkImpl>()->GetVkSampler();
            }
        }
        
        auto& ShaderSPIRV = SPIRVs[ShaderInd];
        PipelineLayout.AllocateResourceSlot(Attribs, VarType, vkImmutableSampler, Resources.GetShaderType(), DescriptorSet, Binding, CacheOffset, ShaderSPIRV);
        VERIFY(DescriptorSet <= std::numeric_limits<decltype(VkResource::DescriptorSet)>::max(), "Descriptor set (", DescriptorSet, ") excceeds maximum representable value");
        VERIFY(Binding       <= std::numeric_limits<decltype(VkResource::Binding)>      ::max(), "Binding (",        Binding,       ") excceeds maximum representable value");

#ifdef _DEBUG
        // Verify that bindings and cache offsets monotonically increase in every descriptor set
        auto Binding_OffsetIt = dbgBindings_CacheOffsets.find(DescriptorSet);
        if(Binding_OffsetIt != dbgBindings_CacheOffsets.end())
        {
            VERIFY(Binding     > Binding_OffsetIt->second.first,  "Binding for descriptor set ", DescriptorSet, " is not strictly monotonic");
            VERIFY(CacheOffset > Binding_OffsetIt->second.second, "Cache offset for descriptor set ", DescriptorSet, " is not strictly monotonic");
        }
        dbgBindings_CacheOffsets[DescriptorSet] = std::make_pair(Binding, CacheOffset);
#endif

        auto& ResInd = CurrResInd[ShaderInd][VarType];
        ::new (&ResLayout.GetResource(VarType, ResInd++)) VkResource(ResLayout, Attribs, VarType, Binding, DescriptorSet, CacheOffset, SamplerInd, vkImmutableSampler != VK_NULL_HANDLE ? 1 : 0);
    };

    // First process uniform buffers for all shader stages to make sure all UBs go first in every descriptor set
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto& Layout = Layouts[s];
        const auto& Resources = *Layout.m_pResources;
        for (Uint32 n = 0; n < Resources.GetNumUBs(); ++n)
        {
            const auto& UB = Resources.GetUB(n);
            auto VarType = GetShaderVariableType(Resources.GetShaderType(), UB.Name, ResourceLayoutDesc);
            if (IsAllowedType(VarType, AllowedTypeBits))
            {
                AddResource(s, Layout, Resources, UB);
            }
        }
    }

    // Second, process all storage buffers
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto& Layout = Layouts[s];
        const auto& Resources = *Layout.m_pResources;
        for (Uint32 n = 0; n < Resources.GetNumSBs(); ++n)
        {
            const auto& SB = Resources.GetSB(n);
            auto VarType = GetShaderVariableType(Resources.GetShaderType(), SB.Name, ResourceLayoutDesc);
            if (IsAllowedType(VarType, AllowedTypeBits))
            {
                AddResource(s, Layout, Resources, SB);
            }
        }
    }

    // Finally, process all other resource types
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto& Layout = Layouts[s];
        const auto& Resources = *Layout.m_pResources;
        Resources.ProcessResources(
            [&](const SPIRVShaderResourceAttribs& UB, Uint32)
            {
                VERIFY_EXPR(UB.Type == SPIRVShaderResourceAttribs::ResourceType::UniformBuffer);
                // Skip
            },
            [&](const SPIRVShaderResourceAttribs& SB, Uint32)
            {
                VERIFY_EXPR(SB.Type == SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer || SB.Type == SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer);
                // Skip
            },
            [&](const SPIRVShaderResourceAttribs& Img, Uint32)
            {
                VERIFY_EXPR(Img.Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage || Img.Type == SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer);
                AddResource(s, Layout, Resources, Img);
            },
            [&](const SPIRVShaderResourceAttribs& SmplImg, Uint32)
            {
                VERIFY_EXPR(SmplImg.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage || SmplImg.Type == SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer);
                AddResource(s, Layout, Resources, SmplImg);
            },
            [&](const SPIRVShaderResourceAttribs& AC, Uint32)
            {
                VERIFY_EXPR(AC.Type == SPIRVShaderResourceAttribs::ResourceType::AtomicCounter);
                AddResource(s, Layout, Resources, AC);
            },
            [&](const SPIRVShaderResourceAttribs& SepSmpl, Uint32)
            {
                VERIFY_EXPR(SepSmpl.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler);
                AddResource(s, Layout, Resources, SepSmpl);
            },
            [&](const SPIRVShaderResourceAttribs& SepImg, Uint32)
            {
                VERIFY_EXPR(SepImg.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage || SepImg.Type == SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer);
                AddResource(s, Layout, Resources, SepImg);
            }
        );
    }

#ifdef _DEBUG
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        auto& Layout = Layouts[s];
        for (SHADER_RESOURCE_VARIABLE_TYPE VarType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC; VarType < SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_RESOURCE_VARIABLE_TYPE>(VarType + 1))
        {
            VERIFY(CurrResInd[s][VarType] == Layout.m_NumResources[VarType], "Not all resources have been initialized, which will cause a crash when dtor is called. This is a bug.");
        }
        // Some immutable samplers may never be initialized if they are not present in shaders
        VERIFY_EXPR(CurrImmutableSamplerInd[s] <= Layout.m_NumImmutableSamplers);
    }
#endif
}


Uint32 ShaderResourceLayoutVk::FindAssignedSampler(const SPIRVShaderResourceAttribs& SepImg,
                                                   Uint32                            CurrResourceCount,
                                                   SHADER_RESOURCE_VARIABLE_TYPE     ImgVarType)const
{
    VERIFY_EXPR(SepImg.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage);
    
    Uint32 SamplerInd = VkResource::InvalidSamplerInd;
    if (m_pResources->IsUsingCombinedSamplers() && SepImg.IsValidSepSamplerAssigned())
    {
        const auto& SepSampler = m_pResources->GetAssignedSepSampler(SepImg);
        for (SamplerInd = 0; SamplerInd < CurrResourceCount; ++SamplerInd)
        {
            const auto& Res = GetResource(ImgVarType, SamplerInd);
            if (Res.SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler && 
                strcmp(Res.SpirvAttribs.Name, SepSampler.Name) == 0)
            {
                VERIFY(ImgVarType == Res.GetVariableType(),
                       "The type (", GetShaderVariableTypeLiteralName(ImgVarType),") of separate image variable '", SepImg.Name,
                       "' is not consistent with the type (", GetShaderVariableTypeLiteralName(Res.GetVariableType()),
                       ") of the separate sampler '", SepSampler.Name, "' that is assigned to it. "
                       "This should never happen as when HLSL-style combined texture samplers are used, the type of the sampler "
                       "is derived from the type of the corresponding separate image.");
                break;
            }
        }
        if (SamplerInd == CurrResourceCount)
        {
            LOG_ERROR("Unable to find separate sampler '", SepSampler.Name, "' assigned to separate image '", SepImg.Name, "' in the list of already created resources. This seems to be a bug.");
            SamplerInd = VkResource::InvalidSamplerInd;
        }
    }
    return SamplerInd;
}


void ShaderResourceLayoutVk::VkResource::UpdateDescriptorHandle(VkDescriptorSet                vkDescrSet,
                                                                uint32_t                       ArrayElement,
                                                                const VkDescriptorImageInfo*   pImageInfo,
                                                                const VkDescriptorBufferInfo*  pBufferInfo,
                                                                const VkBufferView*            pTexelBufferView)const
{
    VERIFY_EXPR(vkDescrSet != VK_NULL_HANDLE);

    VkWriteDescriptorSet WriteDescrSet;
    WriteDescrSet.sType            = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    WriteDescrSet.pNext            = nullptr;
    WriteDescrSet.dstSet           = vkDescrSet;
    WriteDescrSet.dstBinding       = Binding;
    WriteDescrSet.dstArrayElement  = ArrayElement;
    WriteDescrSet.descriptorCount  = 1;
    // descriptorType must be the same type as that specified in VkDescriptorSetLayoutBinding for dstSet at dstBinding. 
    // The type of the descriptor also controls which array the descriptors are taken from. (13.2.4)
    WriteDescrSet.descriptorType   = PipelineLayout::GetVkDescriptorType(SpirvAttribs);
    WriteDescrSet.pImageInfo       = pImageInfo;
    WriteDescrSet.pBufferInfo      = pBufferInfo;
    WriteDescrSet.pTexelBufferView = pTexelBufferView;

    ParentResLayout.m_LogicalDevice.UpdateDescriptorSets(1, &WriteDescrSet, 0, nullptr);
}

template<typename ObjectType, typename TPreUpdateObject>
bool ShaderResourceLayoutVk::VkResource::UpdateCachedResource(ShaderResourceCacheVk::Resource& DstRes,
                                                              RefCntAutoPtr<ObjectType>&&      pObject,
                                                              TPreUpdateObject                 PreUpdateObject)const
{
    // We cannot use ValidatedCast<> here as the resource retrieved from the
    // resource mapping can be of wrong type
    if (pObject)
    {
        if (GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC && DstRes.pObject != nullptr)
        {
            // Do not update resource if one is already bound unless it is dynamic. This may be 
            // dangerous as writing descriptors while they are used by the GPU is an undefined behavior
            return false;
        }

        PreUpdateObject(DstRes.pObject.template RawPtr<const ObjectType>(), pObject.template RawPtr<const ObjectType>());
        DstRes.pObject.Attach(pObject.Detach());
        return true;
    }
    else
    {
        return false;
    }
}

void ShaderResourceLayoutVk::VkResource::CacheUniformBuffer(IDeviceObject*                     pBuffer,
                                                            ShaderResourceCacheVk::Resource&   DstRes, 
                                                            VkDescriptorSet                    vkDescrSet, 
                                                            Uint32                             ArrayInd,
                                                            Uint16&                            DynamicBuffersCounter)const
{
    VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::UniformBuffer, "Uniform buffer resource is expected");
    RefCntAutoPtr<BufferVkImpl> pBufferVk(pBuffer, IID_BufferVk);
#ifdef DEVELOPMENT
    VerifyConstantBufferBinding(SpirvAttribs, GetVariableType(), ArrayInd, pBuffer, pBufferVk.RawPtr(), DstRes.pObject.RawPtr(), ParentResLayout.GetShaderName());
#endif

    auto UpdateDynamicBuffersCounter = [&DynamicBuffersCounter](const BufferVkImpl* pOldBuffer, const BufferVkImpl* pNewBuffer)
    {
        if (pOldBuffer != nullptr && pOldBuffer->GetDesc().Usage == USAGE_DYNAMIC)
        {
            VERIFY(DynamicBuffersCounter > 0, "Dynamic buffers counter must be greater than zero when there is at least one dynamic buffer bound in the resource cache");
            --DynamicBuffersCounter;
        }
        if (pNewBuffer != nullptr && pNewBuffer->GetDesc().Usage == USAGE_DYNAMIC)
            ++DynamicBuffersCounter;
    };
    if (UpdateCachedResource(DstRes, std::move(pBufferVk), UpdateDynamicBuffersCounter))
    {
        // VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER or VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC descriptor type require
        // buffer to be created with VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT

        // Do not update descriptor for a dynamic uniform buffer. All dynamic resource 
        // descriptors are updated at once by CommitDynamicResources() when SRB is committed.
        if (vkDescrSet != VK_NULL_HANDLE && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            VkDescriptorBufferInfo DescrBuffInfo = DstRes.GetUniformBufferDescriptorWriteInfo();
            UpdateDescriptorHandle(vkDescrSet, ArrayInd, nullptr, &DescrBuffInfo, nullptr);
        }
    }
}

void ShaderResourceLayoutVk::VkResource::CacheStorageBuffer(IDeviceObject*                     pBufferView,
                                                            ShaderResourceCacheVk::Resource&   DstRes, 
                                                            VkDescriptorSet                    vkDescrSet, 
                                                            Uint32                             ArrayInd,
                                                            Uint16&                            DynamicBuffersCounter)const
{
    VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer || 
           SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer,
           "Storage buffer resource is expected");

    RefCntAutoPtr<BufferViewVkImpl> pBufferViewVk(pBufferView, IID_BufferViewVk);
#ifdef DEVELOPMENT
    {
        // HLSL buffer SRVs are mapped to storge buffers in GLSL
        auto RequiredViewType = SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer ? BUFFER_VIEW_SHADER_RESOURCE : BUFFER_VIEW_UNORDERED_ACCESS;
        VerifyResourceViewBinding(SpirvAttribs, GetVariableType(), ArrayInd, pBufferView, pBufferViewVk.RawPtr(), {RequiredViewType}, DstRes.pObject.RawPtr(), ParentResLayout.GetShaderName());
    }
#endif

    auto UpdateDynamicBuffersCounter = [&DynamicBuffersCounter](const BufferViewVkImpl* pOldBufferView, const BufferViewVkImpl* pNewBufferView)
    {
        if (pOldBufferView != nullptr && pOldBufferView->GetBuffer<const BufferVkImpl>()->GetDesc().Usage == USAGE_DYNAMIC)
        {
            VERIFY(DynamicBuffersCounter > 0, "Dynamic buffers counter must be greater than zero when there is at least one dynamic buffer bound in the resource cache");
            --DynamicBuffersCounter;
        }
        if (pNewBufferView != nullptr && pNewBufferView->GetBuffer<const BufferVkImpl>()->GetDesc().Usage == USAGE_DYNAMIC)
            ++DynamicBuffersCounter;
    };

    if (UpdateCachedResource(DstRes, std::move(pBufferViewVk), UpdateDynamicBuffersCounter))
    {
        // VK_DESCRIPTOR_TYPE_STORAGE_BUFFER or VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC descriptor type 
        // require buffer to be created with VK_BUFFER_USAGE_STORAGE_BUFFER_BIT (13.2.4)

        // Do not update descriptor for a dynamic storage buffer. All dynamic resource 
        // descriptors are updated at once by CommitDynamicResources() when SRB is committed.
        if (vkDescrSet != VK_NULL_HANDLE && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            VkDescriptorBufferInfo DescrBuffInfo = DstRes.GetStorageBufferDescriptorWriteInfo();
            UpdateDescriptorHandle(vkDescrSet, ArrayInd, nullptr, &DescrBuffInfo, nullptr);
        }
    }
}

void ShaderResourceLayoutVk::VkResource::CacheTexelBuffer(IDeviceObject*                     pBufferView,
                                                          ShaderResourceCacheVk::Resource&   DstRes, 
                                                          VkDescriptorSet                    vkDescrSet,
                                                          Uint32                             ArrayInd,
                                                          Uint16&                            DynamicBuffersCounter)const
{
    VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer || 
           SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer,
           "Uniform or storage buffer resource is expected");

    RefCntAutoPtr<BufferViewVkImpl> pBufferViewVk(pBufferView, IID_BufferViewVk);
#ifdef DEVELOPMENT
    {
        // HLSL buffer SRVs are mapped to storge buffers in GLSL
        auto RequiredViewType = SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer ? BUFFER_VIEW_UNORDERED_ACCESS : BUFFER_VIEW_SHADER_RESOURCE;
        VerifyResourceViewBinding(SpirvAttribs, GetVariableType(), ArrayInd, pBufferView, pBufferViewVk.RawPtr(), {RequiredViewType}, DstRes.pObject.RawPtr(), ParentResLayout.GetShaderName());
    }
#endif

    auto UpdateDynamicBuffersCounter = [&DynamicBuffersCounter](const BufferViewVkImpl* pOldBufferView, const BufferViewVkImpl* pNewBufferView)
    {
        if (pOldBufferView != nullptr && pOldBufferView->GetBuffer<const BufferVkImpl>()->GetDesc().Usage == USAGE_DYNAMIC)
        {
            VERIFY(DynamicBuffersCounter > 0, "Dynamic buffers counter must be greater than zero when there is at least one dynamic buffer bound in the resource cache");
            --DynamicBuffersCounter;
        }
        if (pNewBufferView != nullptr && pNewBufferView->GetBuffer<const BufferVkImpl>()->GetDesc().Usage == USAGE_DYNAMIC)
            ++DynamicBuffersCounter;
    };

    if (UpdateCachedResource(DstRes, std::move(pBufferViewVk), UpdateDynamicBuffersCounter))
    {
        // The following bits must have been set at buffer creation time:
        //  * VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER  ->  VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT
        //  * VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER  ->  VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT

        // Do not update descriptor for a dynamic texel buffer. All dynamic resource descriptors 
        // are updated at once by CommitDynamicResources() when SRB is committed.
        if (vkDescrSet != VK_NULL_HANDLE && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            VkBufferView BuffView = DstRes.pObject.RawPtr<BufferViewVkImpl>()->GetVkBufferView();
            UpdateDescriptorHandle(vkDescrSet, ArrayInd, nullptr, nullptr, &BuffView);
        }
    }
}

template<typename TCacheSampler>
void ShaderResourceLayoutVk::VkResource::CacheImage(IDeviceObject*                   pTexView,
                                                    ShaderResourceCacheVk::Resource& DstRes,
                                                    VkDescriptorSet                  vkDescrSet,
                                                    Uint32                           ArrayInd,
                                                    TCacheSampler                    CacheSampler)const
{
    VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage  || 
           SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage ||
           SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage,
           "Storage image, separate image or sampled image resource is expected");

    RefCntAutoPtr<TextureViewVkImpl> pTexViewVk0(pTexView, IID_TextureViewVk);
#ifdef DEVELOPMENT
    {
        // HLSL buffer SRVs are mapped to storge buffers in GLSL
        auto RequiredViewType = SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::StorageImage ? TEXTURE_VIEW_UNORDERED_ACCESS : TEXTURE_VIEW_SHADER_RESOURCE;
        VerifyResourceViewBinding(SpirvAttribs, GetVariableType(), ArrayInd, pTexView, pTexViewVk0.RawPtr(), {RequiredViewType}, DstRes.pObject.RawPtr(), ParentResLayout.GetShaderName());
    }
#endif
    if (UpdateCachedResource(DstRes, std::move(pTexViewVk0), [](const TextureViewVkImpl*, const TextureViewVkImpl*){}))
    {
        // We can do RawPtr here safely since UpdateCachedResource() returned true
        auto* pTexViewVk = DstRes.pObject.RawPtr<TextureViewVkImpl>();
#ifdef DEVELOPMENT
        if (SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SampledImage && !IsImmutableSamplerAssigned())
        {
            if (pTexViewVk->GetSampler() == nullptr)
            {
                LOG_ERROR_MESSAGE("Error binding texture view '", pTexViewVk->GetDesc().Name, "' to variable '", SpirvAttribs.GetPrintName(ArrayInd),
                                  "' in shader '", ParentResLayout.GetShaderName(), "'. No sampler is assigned to the view");
            }
        }
#endif

        // Do not update descriptor for a dynamic image. All dynamic resource descriptors 
        // are updated at once by CommitDynamicResources() when SRB is committed.
        if (vkDescrSet != VK_NULL_HANDLE && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            VkDescriptorImageInfo DescrImgInfo = DstRes.GetImageDescriptorWriteInfo(IsImmutableSamplerAssigned());
            UpdateDescriptorHandle(vkDescrSet, ArrayInd, &DescrImgInfo, nullptr, nullptr);
        }

        if (SamplerInd != InvalidSamplerInd)
        {
            VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage,
                   "Only separate images can be assigned separate samplers when using HLSL-style combined samplers.");
            VERIFY(!IsImmutableSamplerAssigned(), "Separate image can't be assigned an immutable sampler.");
            const auto& SamplerAttribs = ParentResLayout.GetResource(GetVariableType(), SamplerInd);
            VERIFY_EXPR(SamplerAttribs.SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler);
            if (!SamplerAttribs.IsImmutableSamplerAssigned())
            {
                auto* pSampler = pTexViewVk->GetSampler();
                if (pSampler != nullptr)
                {
                    CacheSampler(SamplerAttribs, pSampler);
                }
                else
                {
                    LOG_ERROR_MESSAGE( "Failed to bind sampler to sampler variable '", SamplerAttribs.SpirvAttribs.Name,
                                       "' assigned to separate image '", SpirvAttribs.GetPrintName(ArrayInd), "' in shader '",
                                       ParentResLayout.GetShaderName(), "': no sampler is set in texture view '", pTexViewVk->GetDesc().Name, '\'');                    \
                }
            }
        }
    }
}

void ShaderResourceLayoutVk::VkResource::CacheSeparateSampler(IDeviceObject*                    pSampler,
                                                              ShaderResourceCacheVk::Resource&  DstRes,
                                                              VkDescriptorSet                   vkDescrSet,
                                                              Uint32                            ArrayInd)const
{
    VERIFY(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler, "Separate sampler resource is expected");
    VERIFY(!IsImmutableSamplerAssigned(), "This separate sampler is assigned an immutable sampler");

    RefCntAutoPtr<SamplerVkImpl> pSamplerVk(pSampler, IID_Sampler);
#ifdef DEVELOPMENT
    if (pSampler != nullptr && pSamplerVk == nullptr)
    {
        LOG_ERROR_MESSAGE("Failed to bind object '", pSampler->GetDesc().Name, "' to variable '", SpirvAttribs.GetPrintName(ArrayInd),
                          "' in shader '", ParentResLayout.GetShaderName(), "'. Unexpected object type: sampler is expected");
    }
    if (GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC && DstRes.pObject != nullptr && DstRes.pObject != pSamplerVk)
    {
        auto VarTypeStr = GetShaderVariableTypeLiteralName(GetVariableType());
        LOG_ERROR_MESSAGE("Non-null sampler is already bound to ", VarTypeStr, " shader variable '", SpirvAttribs.GetPrintName(ArrayInd),
                          "' in shader '", ParentResLayout.GetShaderName(), "'. Attempting to bind another sampler or null is an error and may "
                          "cause unpredicted behavior. Use another shader resource binding instance or label the variable as dynamic.");
    }
#endif
    if (UpdateCachedResource(DstRes, std::move(pSamplerVk), [](const SamplerVkImpl*, const SamplerVkImpl*){}))
    {
        // Do not update descriptor for a dynamic sampler. All dynamic resource descriptors 
        // are updated at once by CommitDynamicResources() when SRB is committed.
        if (vkDescrSet != VK_NULL_HANDLE && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            VkDescriptorImageInfo DescrImgInfo = DstRes.GetSamplerDescriptorWriteInfo();
            UpdateDescriptorHandle(vkDescrSet, ArrayInd, &DescrImgInfo, nullptr, nullptr);
        }
    }
}


void ShaderResourceLayoutVk::VkResource::BindResource(IDeviceObject* pObj, Uint32 ArrayIndex, ShaderResourceCacheVk& ResourceCache)const
{
    VERIFY_EXPR(ArrayIndex < SpirvAttribs.ArraySize);

    auto& DstDescrSet = ResourceCache.GetDescriptorSet(DescriptorSet);
    auto vkDescrSet = DstDescrSet.GetVkDescriptorSet();
#ifdef _DEBUG
    if (ResourceCache.DbgGetContentType() == ShaderResourceCacheVk::DbgCacheContentType::SRBResources)
    {
        if(VariableType == SHADER_RESOURCE_VARIABLE_TYPE_STATIC || VariableType == SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
        {
            VERIFY(vkDescrSet != VK_NULL_HANDLE, "Static and mutable variables must have valid vulkan descriptor set assigned");
            // Dynamic variables do not have vulkan descriptor set only until they are assigned one the first time
        }
    }
    else if (ResourceCache.DbgGetContentType() == ShaderResourceCacheVk::DbgCacheContentType::StaticShaderResources)
    {
        VERIFY(vkDescrSet == VK_NULL_HANDLE, "Static shader resource cache should not have vulkan descriptor set allocation");
    }
    else
    {
        UNEXPECTED("Unexpected shader resource cache content type");
    }
#endif
    auto& DstRes = DstDescrSet.GetResource(CacheOffset + ArrayIndex);
    VERIFY(DstRes.Type == SpirvAttribs.Type, "Inconsistent types");

    if( pObj )
    {
        switch (SpirvAttribs.Type)
        {
            case SPIRVShaderResourceAttribs::ResourceType::UniformBuffer:
                CacheUniformBuffer(pObj, DstRes, vkDescrSet, ArrayIndex, ResourceCache.GetDynamicBuffersCounter());
            break;

            case SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer:
                CacheStorageBuffer(pObj, DstRes, vkDescrSet, ArrayIndex, ResourceCache.GetDynamicBuffersCounter());
            break;

            case SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer:
                CacheTexelBuffer(pObj, DstRes, vkDescrSet, ArrayIndex, ResourceCache.GetDynamicBuffersCounter());
                break;

            case SPIRVShaderResourceAttribs::ResourceType::StorageImage:
            case SPIRVShaderResourceAttribs::ResourceType::SeparateImage:
            case SPIRVShaderResourceAttribs::ResourceType::SampledImage:
                CacheImage(pObj, DstRes, vkDescrSet, ArrayIndex, 
                    [&](const VkResource& SeparateSampler, ISampler* pSampler)
                    {
                        VERIFY(!SeparateSampler.IsImmutableSamplerAssigned(), "Separate sampler '", SeparateSampler.SpirvAttribs.Name, "' is assigned an immutable sampler");
                        VERIFY_EXPR(SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateImage);
                        DEV_CHECK_ERR(SeparateSampler.SpirvAttribs.ArraySize == 1 || SeparateSampler.SpirvAttribs.ArraySize == SpirvAttribs.ArraySize,
                                      "Array size (", SeparateSampler.SpirvAttribs.ArraySize,") of separate sampler variable '",
                                      SeparateSampler.SpirvAttribs.Name, "' must be one or the same as the array size (", SpirvAttribs.ArraySize,
                                      ") of separate image variable '", SpirvAttribs.Name, "' it is assigned to");
                        Uint32 SamplerArrInd = SeparateSampler.SpirvAttribs.ArraySize == 1 ? 0 : ArrayIndex;
                        SeparateSampler.BindResource(pSampler, SamplerArrInd, ResourceCache);
                    }
                );
            break;

            case SPIRVShaderResourceAttribs::ResourceType::SeparateSampler:
                if (!IsImmutableSamplerAssigned())
                {
                    CacheSeparateSampler(pObj, DstRes, vkDescrSet, ArrayIndex);
                }
                else
                {
                    // Immutable samplers are permanently bound into the set layout; later binding a sampler 
                    // into an immutable sampler slot in a descriptor set is not allowed (13.2.1)
                    LOG_ERROR_MESSAGE("Attempting to assign a sampler to an immutable sampler '", SpirvAttribs.Name, '\'');
                }
            break;

            default: UNEXPECTED("Unknown resource type ", static_cast<Int32>(SpirvAttribs.Type));
        }
    }
    else
    {
        if (DstRes.pObject && GetVariableType() != SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
        {
            LOG_ERROR_MESSAGE( "Shader variable '", SpirvAttribs.Name, "' in shader '", ParentResLayout.GetShaderName(), "' is not dynamic but being unbound. This is an error and may cause unpredicted behavior. Use another shader resource binding instance or label shader variable as dynamic if you need to bind another resource." );
        }

        DstRes.pObject.Release();
    }
}

bool ShaderResourceLayoutVk::VkResource::IsBound(Uint32 ArrayIndex, const ShaderResourceCacheVk& ResourceCache)const
{
    VERIFY_EXPR(ArrayIndex < SpirvAttribs.ArraySize);

    if (DescriptorSet < ResourceCache.GetNumDescriptorSets())
    {
        const auto& Set = ResourceCache.GetDescriptorSet(DescriptorSet);
        if (CacheOffset + ArrayIndex < Set.GetSize())
        {
            const auto& CachedRes = Set.GetResource(CacheOffset + ArrayIndex);
            return CachedRes.pObject != nullptr;
        }
    }

    return false;
}


void ShaderResourceLayoutVk::InitializeStaticResources(const ShaderResourceLayoutVk& SrcLayout,
                                                       const ShaderResourceCacheVk&  SrcResourceCache,
                                                       ShaderResourceCacheVk&        DstResourceCache)const
{
    auto NumStaticResources = m_NumResources[SHADER_RESOURCE_VARIABLE_TYPE_STATIC];
    VERIFY(NumStaticResources == SrcLayout.m_NumResources[SHADER_RESOURCE_VARIABLE_TYPE_STATIC], "Inconsistent number of static resources");
    VERIFY(SrcLayout.m_pResources->GetShaderType() == m_pResources->GetShaderType(), "Incosistent shader types");

    // Static shader resources are stored in one large continuous descriptor set
    for (Uint32 r=0; r < NumStaticResources; ++r)
    {
        // Get resource attributes
        const auto& DstRes = GetResource(SHADER_RESOURCE_VARIABLE_TYPE_STATIC, r);
        const auto& SrcRes = SrcLayout.GetResource(SHADER_RESOURCE_VARIABLE_TYPE_STATIC, r);
        VERIFY(SrcRes.Binding == SrcRes.SpirvAttribs.Type, "Unexpected binding");
        VERIFY(SrcRes.SpirvAttribs.ArraySize == DstRes.SpirvAttribs.ArraySize, "Inconsistent array size");

        if (DstRes.SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler && 
            DstRes.IsImmutableSamplerAssigned())
            continue; // Skip immutable samplers

        for (Uint32 ArrInd = 0; ArrInd < DstRes.SpirvAttribs.ArraySize; ++ArrInd)
        {
            auto SrcOffset = SrcRes.CacheOffset + ArrInd;
            const auto& SrcCachedSet = SrcResourceCache.GetDescriptorSet(SrcRes.DescriptorSet);
            const auto& SrcCachedRes = SrcCachedSet.GetResource(SrcOffset);
            IDeviceObject* pObject = SrcCachedRes.pObject.RawPtr<IDeviceObject>();
            if (!pObject)
                LOG_ERROR_MESSAGE("No resource is assigned to static shader variable '", SrcRes.SpirvAttribs.GetPrintName(ArrInd), "' in shader '", GetShaderName(), "'.");
            
            auto DstOffset = DstRes.CacheOffset + ArrInd;
            IDeviceObject* pCachedResource = DstResourceCache.GetDescriptorSet(DstRes.DescriptorSet).GetResource(DstOffset).pObject;
            if (pCachedResource != pObject)
            {
                VERIFY(pCachedResource == nullptr, "Static resource has already been initialized, and the resource to be assigned from the shader does not match previously assigned resource");
                DstRes.BindResource(pObject, ArrInd, DstResourceCache);
            }
        }
    }
}


#ifdef DEVELOPMENT
bool ShaderResourceLayoutVk::dvpVerifyBindings(const ShaderResourceCacheVk& ResourceCache)const
{
    bool BindingsOK = true;
    for(SHADER_RESOURCE_VARIABLE_TYPE VarType = SHADER_RESOURCE_VARIABLE_TYPE_STATIC; VarType < SHADER_RESOURCE_VARIABLE_TYPE_NUM_TYPES; VarType = static_cast<SHADER_RESOURCE_VARIABLE_TYPE>(VarType+1))
    {
        for(Uint32 r=0; r < m_NumResources[VarType]; ++r)
        {
            const auto& Res = GetResource(VarType, r);
            VERIFY(Res.GetVariableType() == VarType, "Unexpected variable type");
            for(Uint32 ArrInd = 0; ArrInd < Res.SpirvAttribs.ArraySize; ++ArrInd)
            {
                const auto& CachedDescrSet = ResourceCache.GetDescriptorSet(Res.DescriptorSet);
                const auto& CachedRes = CachedDescrSet.GetResource(Res.CacheOffset + ArrInd);
                VERIFY(CachedRes.Type == Res.SpirvAttribs.Type, "Inconsistent types");
                if (CachedRes.pObject == nullptr && 
                   !(Res.SpirvAttribs.Type == SPIRVShaderResourceAttribs::ResourceType::SeparateSampler && Res.IsImmutableSamplerAssigned()))
                {
                    LOG_ERROR_MESSAGE("No resource is bound to ", GetShaderVariableTypeLiteralName(Res.GetVariableType()), " variable '", Res.SpirvAttribs.GetPrintName(ArrInd), "' in shader '", GetShaderName(), "'");
                    BindingsOK = false;
                }
#ifdef _DEBUG
                auto vkDescSet = CachedDescrSet.GetVkDescriptorSet();
                auto dbgCacheContentType = ResourceCache.DbgGetContentType();
                if (dbgCacheContentType == ShaderResourceCacheVk::DbgCacheContentType::StaticShaderResources)
                    VERIFY(vkDescSet == VK_NULL_HANDLE, "Static resource cache should never have vulkan descriptor set");
                else if (dbgCacheContentType == ShaderResourceCacheVk::DbgCacheContentType::SRBResources)
                {
                    if (VarType == SHADER_RESOURCE_VARIABLE_TYPE_STATIC || VarType == SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE)
                    {
                        VERIFY(vkDescSet != VK_NULL_HANDLE, "Static and mutable variables must have valid vulkan descriptor set assigned");
                    }
                    else if (VarType == SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC)
                    {
                        VERIFY(vkDescSet == VK_NULL_HANDLE, "Dynamic variables must not be assigned a vulkan descriptor set");
                    }
                }
                else
                    UNEXPECTED("Unexpected cache content type");
#endif
            }
        }
    }
    return BindingsOK;
}
#endif

void ShaderResourceLayoutVk::InitializeResourceMemoryInCache(ShaderResourceCacheVk& ResourceCache)const
{
    auto TotalResources = GetTotalResourceCount();
    for (Uint32 r = 0; r < TotalResources; ++r)
    {
        const auto& Res = GetResource(r);
        ResourceCache.InitializeResources(Res.DescriptorSet, Res.CacheOffset, Res.SpirvAttribs.ArraySize, Res.SpirvAttribs.Type);
    }
}

void ShaderResourceLayoutVk::CommitDynamicResources(const ShaderResourceCacheVk& ResourceCache, 
                                                    VkDescriptorSet              vkDynamicDescriptorSet)const
{
    Uint32 NumDynamicResources = m_NumResources[SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC];
    VERIFY(NumDynamicResources != 0, "This shader resource layout does not contain dynamic resources");
    VERIFY_EXPR(vkDynamicDescriptorSet != VK_NULL_HANDLE);

#ifdef _DEBUG
    static constexpr size_t ImgUpdateBatchSize          = 4;
    static constexpr size_t BuffUpdateBatchSize         = 2;
    static constexpr size_t TexelBuffUpdateBatchSize    = 2;
    static constexpr size_t WriteDescriptorSetBatchSize = 2;
#else
    static constexpr size_t ImgUpdateBatchSize          = 128;
    static constexpr size_t BuffUpdateBatchSize         = 64;
    static constexpr size_t TexelBuffUpdateBatchSize    = 32;
    static constexpr size_t WriteDescriptorSetBatchSize = 32;
#endif

    // Do not zero-initiaize arrays!
    std::array<VkDescriptorImageInfo,  ImgUpdateBatchSize>          DescrImgInfoArr;
    std::array<VkDescriptorBufferInfo, BuffUpdateBatchSize>         DescrBuffInfoArr;
    std::array<VkBufferView,           TexelBuffUpdateBatchSize>    DescrBuffViewArr;
    std::array<VkWriteDescriptorSet,   WriteDescriptorSetBatchSize> WriteDescrSetArr;

    Uint32 ResNum = 0, ArrElem = 0;
    auto DescrImgIt      = DescrImgInfoArr.begin();
    auto DescrBuffIt     = DescrBuffInfoArr.begin();
    auto BuffViewIt      = DescrBuffViewArr.begin();
    auto WriteDescrSetIt = WriteDescrSetArr.begin();

#ifdef _DEBUG
    Int32 DynamicDescrSetIndex = -1;
#endif

    while(ResNum < NumDynamicResources)
    {
        const auto& Res = GetResource(SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC, ResNum);
        VERIFY_EXPR(Res.GetVariableType() == SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC);
#ifdef _DEBUG
        if(DynamicDescrSetIndex < 0)
            DynamicDescrSetIndex = Res.DescriptorSet;
        else
            VERIFY(DynamicDescrSetIndex == Res.DescriptorSet, "Inconsistent dynamic resource desriptor set index");
#endif
        const auto& SetResources = ResourceCache.GetDescriptorSet(Res.DescriptorSet);
        WriteDescrSetIt->sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        WriteDescrSetIt->pNext = nullptr;
        VERIFY(SetResources.GetVkDescriptorSet() == VK_NULL_HANDLE, "Dynamic descriptor set must not be assigned to the resource cache");
        WriteDescrSetIt->dstSet = vkDynamicDescriptorSet;
        VERIFY(WriteDescrSetIt->dstSet != VK_NULL_HANDLE, "Vulkan descriptor set must not be null");
        WriteDescrSetIt->dstBinding = Res.Binding;
        WriteDescrSetIt->dstArrayElement = ArrElem;
        // descriptorType must be the same type as that specified in VkDescriptorSetLayoutBinding for dstSet at dstBinding. 
        // The type of the descriptor also controls which array the descriptors are taken from. (13.2.4)
        WriteDescrSetIt->descriptorType = PipelineLayout::GetVkDescriptorType(Res.SpirvAttribs);
        
        // For every resource type, try to batch as many descriptor updates as we can
        switch (Res.SpirvAttribs.Type)
        {
            case SPIRVShaderResourceAttribs::ResourceType::UniformBuffer:
                WriteDescrSetIt->pBufferInfo = &(*DescrBuffIt);
                while (ArrElem < Res.SpirvAttribs.ArraySize && DescrBuffIt != DescrBuffInfoArr.end())
                {
                    const auto& CachedRes = SetResources.GetResource(Res.CacheOffset + ArrElem);
                    *DescrBuffIt = CachedRes.GetUniformBufferDescriptorWriteInfo();
                    ++DescrBuffIt;
                    ++ArrElem;
                }
            break;
            
            case SPIRVShaderResourceAttribs::ResourceType::ROStorageBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::RWStorageBuffer:
                WriteDescrSetIt->pBufferInfo = &(*DescrBuffIt);
                while (ArrElem < Res.SpirvAttribs.ArraySize && DescrBuffIt != DescrBuffInfoArr.end())
                {
                    const auto& CachedRes = SetResources.GetResource(Res.CacheOffset + ArrElem);
                    *DescrBuffIt = CachedRes.GetStorageBufferDescriptorWriteInfo();
                    ++DescrBuffIt;
                    ++ArrElem;
                }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::UniformTexelBuffer:
            case SPIRVShaderResourceAttribs::ResourceType::StorageTexelBuffer:
                WriteDescrSetIt->pTexelBufferView = &(*BuffViewIt);
                while (ArrElem < Res.SpirvAttribs.ArraySize && BuffViewIt != DescrBuffViewArr.end())
                {
                    const auto& CachedRes = SetResources.GetResource(Res.CacheOffset + ArrElem);
                    *BuffViewIt = CachedRes.GetBufferViewWriteInfo();
                    ++BuffViewIt;
                    ++ArrElem;
                }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::SeparateImage:
            case SPIRVShaderResourceAttribs::ResourceType::StorageImage:
            case SPIRVShaderResourceAttribs::ResourceType::SampledImage:
                WriteDescrSetIt->pImageInfo = &(*DescrImgIt);
                while (ArrElem < Res.SpirvAttribs.ArraySize && DescrImgIt != DescrImgInfoArr.end())
                {
                    const auto& CachedRes = SetResources.GetResource(Res.CacheOffset + ArrElem);
                    *DescrImgIt = CachedRes.GetImageDescriptorWriteInfo(Res.IsImmutableSamplerAssigned());
                    ++DescrImgIt;
                    ++ArrElem;
                }
            break;

            case SPIRVShaderResourceAttribs::ResourceType::AtomicCounter:
                // Do nothing
            break;
           

            case SPIRVShaderResourceAttribs::ResourceType::SeparateSampler:
                // Immutable samplers are permanently bound into the set layout; later binding a sampler 
                // into an immutable sampler slot in a descriptor set is not allowed (13.2.1)
                if (!Res.IsImmutableSamplerAssigned())
                {
                    WriteDescrSetIt->pImageInfo = &(*DescrImgIt);
                    while (ArrElem < Res.SpirvAttribs.ArraySize && DescrImgIt != DescrImgInfoArr.end())
                    {
                        const auto& CachedRes = SetResources.GetResource(Res.CacheOffset + ArrElem);
                        *DescrImgIt = CachedRes.GetSamplerDescriptorWriteInfo();
                        ++DescrImgIt;
                        ++ArrElem;
                    }
                }
                else
                {
                    ArrElem = Res.SpirvAttribs.ArraySize;
                    WriteDescrSetIt->dstArrayElement = Res.SpirvAttribs.ArraySize;
                }
            break;

            default:
                UNEXPECTED("Unexpected resource type");
        }

        WriteDescrSetIt->descriptorCount = ArrElem - WriteDescrSetIt->dstArrayElement;
        if (ArrElem == Res.SpirvAttribs.ArraySize)
        {
            ArrElem = 0;
            ++ResNum;
        }
        // descriptorCount == 0 for immutable separate samplers
        if (WriteDescrSetIt->descriptorCount > 0)
            ++WriteDescrSetIt;

        // If we ran out of space in any of the arrays or if we processed all resources,
        // flush pending updates and reset iterators
        if (ResNum          == NumDynamicResources    || 
            DescrImgIt      == DescrImgInfoArr.end()  || 
            DescrBuffIt     == DescrBuffInfoArr.end() ||
            BuffViewIt      == DescrBuffViewArr.end() ||
            WriteDescrSetIt == WriteDescrSetArr.end())
        {
            auto DescrWriteCount = static_cast<Uint32>(std::distance(WriteDescrSetArr.begin(), WriteDescrSetIt));
            if (DescrWriteCount > 0)
                m_LogicalDevice.UpdateDescriptorSets(DescrWriteCount, WriteDescrSetArr.data(), 0, nullptr);

            DescrImgIt  = DescrImgInfoArr.begin();
            DescrBuffIt = DescrBuffInfoArr.begin();
            BuffViewIt  = DescrBuffViewArr.begin();
            WriteDescrSetIt = WriteDescrSetArr.begin();
        }
    }
}

}
