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
#include "EngineMemory.h"
#include "StringTools.h"
#include "ShaderResources.h"
#include "HashUtils.h"
#include "ShaderResourceVariableBase.h"
#include "Align.h"

namespace Diligent
{

ShaderResources::~ShaderResources()
{
    for(Uint32 n=0; n < GetNumCBs(); ++n)
        GetCB(n).~D3DShaderResourceAttribs();

    for(Uint32 n=0; n < GetNumTexSRV(); ++n)
        GetTexSRV(n).~D3DShaderResourceAttribs();

    for(Uint32 n=0; n < GetNumTexUAV(); ++n)
        GetTexUAV(n).~D3DShaderResourceAttribs();

    for(Uint32 n=0; n < GetNumBufSRV(); ++n)
        GetBufSRV(n).~D3DShaderResourceAttribs();

    for(Uint32 n=0; n < GetNumBufUAV(); ++n)
        GetBufUAV(n).~D3DShaderResourceAttribs();

    for(Uint32 n=0; n < GetNumSamplers(); ++n)
        GetSampler(n).~D3DShaderResourceAttribs();
}

void ShaderResources::AllocateMemory(IMemoryAllocator&                Allocator, 
                                     const D3DShaderResourceCounters& ResCounters,
                                     size_t                           ResourceNamesPoolSize)
{
    Uint32 CurrentOffset = 0;
    auto AdvanceOffset = [&CurrentOffset](Uint32 NumResources)
    {
        constexpr Uint32 MaxOffset = std::numeric_limits<OffsetType>::max();
        VERIFY(CurrentOffset <= MaxOffset, "Current offser (", CurrentOffset, ") exceeds max allowed value (", MaxOffset, ")");
        auto Offset = static_cast<OffsetType>(CurrentOffset);
        CurrentOffset += NumResources;
        return Offset;
    };

    auto CBOffset    = AdvanceOffset(ResCounters.NumCBs);       (void)CBOffset; // To suppress warning
    m_TexSRVOffset   = AdvanceOffset(ResCounters.NumTexSRVs);
    m_TexUAVOffset   = AdvanceOffset(ResCounters.NumTexUAVs);
    m_BufSRVOffset   = AdvanceOffset(ResCounters.NumBufSRVs);
    m_BufUAVOffset   = AdvanceOffset(ResCounters.NumBufUAVs);
    m_SamplersOffset = AdvanceOffset(ResCounters.NumSamplers);
    m_TotalResources = AdvanceOffset(0);

    auto AlignedResourceNamesPoolSize = Align(ResourceNamesPoolSize, sizeof(void*));
    auto MemorySize = m_TotalResources * sizeof(D3DShaderResourceAttribs) + AlignedResourceNamesPoolSize * sizeof(char);

    VERIFY_EXPR(GetNumCBs()     == ResCounters.NumCBs);
    VERIFY_EXPR(GetNumTexSRV()  == ResCounters.NumTexSRVs);
    VERIFY_EXPR(GetNumTexUAV()  == ResCounters.NumTexUAVs);
    VERIFY_EXPR(GetNumBufSRV()  == ResCounters.NumBufSRVs);
    VERIFY_EXPR(GetNumBufUAV()  == ResCounters.NumBufUAVs);
    VERIFY_EXPR(GetNumSamplers()== ResCounters.NumSamplers);

    if( MemorySize )
    {
        auto* pRawMem = ALLOCATE_RAW(Allocator, "Allocator for shader resources", MemorySize );
        m_MemoryBuffer = std::unique_ptr< void, STDDeleterRawMem<void> >(pRawMem, Allocator);
        char* NamesPool = reinterpret_cast<char*>(reinterpret_cast<D3DShaderResourceAttribs*>(pRawMem) + m_TotalResources);
        m_ResourceNames.AssignMemory(NamesPool, ResourceNamesPoolSize);
    }    
}

SHADER_RESOURCE_VARIABLE_TYPE ShaderResources::FindVariableType(const D3DShaderResourceAttribs&   ResourceAttribs,
                                                                const PipelineResourceLayoutDesc& ResourceLayout)const
{
    if (ResourceAttribs.GetInputType() == D3D_SIT_SAMPLER)
    {
        // Only use CombinedSamplerSuffix when looking for the sampler variable type
        return GetShaderVariableType(m_ShaderType, ResourceLayout.DefaultVariableType, ResourceLayout.Variables, ResourceLayout.NumVariables,
                                     [&](const char* VarName)
                                     {
                                         return StreqSuff(ResourceAttribs.Name, VarName, m_SamplerSuffix);
                                     });
    }
    else
    {
        return GetShaderVariableType(m_ShaderType, ResourceAttribs.Name, ResourceLayout);
    }
}

Int32 ShaderResources::FindStaticSampler(const D3DShaderResourceAttribs&   ResourceAttribs,
                                         const PipelineResourceLayoutDesc& ResourceLayoutDesc,
                                         bool                              LogStaticSamplerArrayError)const
{
    VERIFY(ResourceAttribs.GetInputType() == D3D_SIT_SAMPLER, "Sampler is expected");

    auto StaticSamplerInd = 
        Diligent::FindStaticSampler(ResourceLayoutDesc.StaticSamplers,
                                    ResourceLayoutDesc.NumStaticSamplers,
                                    m_ShaderType,
                                    ResourceAttribs.Name,
                                    m_SamplerSuffix);

    if (StaticSamplerInd >= 0 && ResourceAttribs.BindCount > 1)
    {
        Uint32 ShaderMajorVersion = 0;
        Uint32 ShaderMinorVersion = 0;
        GetShaderModel(ShaderMajorVersion, ShaderMinorVersion);
        if (ShaderMajorVersion >= 6 || ShaderMajorVersion >= 5 && ShaderMinorVersion >= 1)
        {
            if (LogStaticSamplerArrayError)
            {
                LOG_ERROR_MESSAGE("Static sampler '", ResourceAttribs.Name, '[', ResourceAttribs.BindCount, "]' will be ignored because static "
                                  "sampler arrays are not allowed in shader model 5.1 and above. Compile the shader using shader "
                                  "model 5.0 or use non-array sampler variable.");
            }
            StaticSamplerInd = -1;
        }
    }

    return StaticSamplerInd;
}


D3DShaderResourceCounters ShaderResources::CountResources(const PipelineResourceLayoutDesc&    ResourceLayout,
                                                          const SHADER_RESOURCE_VARIABLE_TYPE* AllowedVarTypes,
                                                          Uint32                               NumAllowedTypes,
                                                          bool                                 CountStaticSamplers)const noexcept
{
    auto AllowedTypeBits = GetAllowedTypeBits(AllowedVarTypes, NumAllowedTypes);

    D3DShaderResourceCounters Counters;
    ProcessResources(
        [&](const D3DShaderResourceAttribs& CB, Uint32)
        {
            auto VarType = FindVariableType(CB, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
                ++Counters.NumCBs;
        },
        [&](const D3DShaderResourceAttribs& Sam, Uint32)
        {
            auto VarType = FindVariableType(Sam, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
            {
                if (!CountStaticSamplers)
                {
                    constexpr bool LogStaticSamplerArrayError = false;
                    if (FindStaticSampler(Sam, ResourceLayout, LogStaticSamplerArrayError) >= 0)
                        return; // Skip static sampler if requested
                }
                ++Counters.NumSamplers;
            }
        },
        [&](const D3DShaderResourceAttribs& TexSRV, Uint32)
        {
            auto VarType = FindVariableType(TexSRV, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
                ++Counters.NumTexSRVs;
        },
        [&](const D3DShaderResourceAttribs& TexUAV, Uint32)
        {
            auto VarType = FindVariableType(TexUAV, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
                ++Counters.NumTexUAVs;
        },
        [&](const D3DShaderResourceAttribs& BufSRV, Uint32)
        {
            auto VarType = FindVariableType(BufSRV, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
                ++Counters.NumBufSRVs;
        },
        [&](const D3DShaderResourceAttribs& BufUAV, Uint32)
        {
            auto VarType = FindVariableType(BufUAV, ResourceLayout);
            if (IsAllowedType(VarType, AllowedTypeBits))
                ++Counters.NumBufUAVs;
        }
    );

    return Counters;
}

#ifdef DEVELOPMENT
void ShaderResources::DvpVerifyResourceLayout(const PipelineResourceLayoutDesc& ResourceLayout,
                                              const ShaderResources* const      pShaderResources[],
                                              Uint32                            NumShaders)
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

    for (Uint32 v = 0; v < ResourceLayout.NumVariables; ++v)
    {
        const auto& VarDesc = ResourceLayout.Variables[v];
        if (VarDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
        {
            LOG_WARNING_MESSAGE("No allowed shader stages are specified for ", GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name, "'.");
            continue;
        }

        bool VariableFound = false;
        for (Uint32 s = 0; s < NumShaders && !VariableFound; ++s)
        {
            const auto& Resources = *pShaderResources[s];
            if( (VarDesc.ShaderStages & Resources.GetShaderType()) == 0)
                continue;
            
            const auto UseCombinedTextureSamplers = Resources.IsUsingCombinedTextureSamplers();
            for (Uint32 n=0; n < Resources.m_TotalResources && !VariableFound; ++n)
            {
                const auto& Res = Resources.GetResAttribs(n, Resources.m_TotalResources, 0);

                // Skip samplers if combined texture samplers are used as 
                // in this case they are not treated as independent variables
                if (UseCombinedTextureSamplers && Res.GetInputType() == D3D_SIT_SAMPLER)
                    continue;

                VariableFound = (strcmp(Res.Name, VarDesc.Name) == 0);
            }
        }

        if (!VariableFound)
        {
            LOG_WARNING_MESSAGE(GetShaderVariableTypeLiteralName(VarDesc.Type), " variable '", VarDesc.Name,
                                "' is not found in any of the designated shader stages: ",
                                GetAllowedShadersString(VarDesc.ShaderStages));
        }
    }

    for (Uint32 sam = 0; sam < ResourceLayout.NumStaticSamplers; ++sam)
    {
        const auto& StSamDesc = ResourceLayout.StaticSamplers[sam];
        if (StSamDesc.ShaderStages == SHADER_TYPE_UNKNOWN)
        {
            LOG_WARNING_MESSAGE("No allowed shader stages are specified for static sampler '", StSamDesc.SamplerOrTextureName, "'.");
            continue;
        }

        const auto* TexOrSamName = StSamDesc.SamplerOrTextureName;
        
        bool StaticSamplerFound = false;
        for (Uint32 s = 0; s < NumShaders && !StaticSamplerFound; ++s)
        {
            const auto& Resources = *pShaderResources[s];
            if ( (StSamDesc.ShaderStages & Resources.GetShaderType()) == 0)
                continue;

            // Look for static sampler.
            // In case HLSL-style combined image samplers are used, the condition is  Sampler.Name == "g_Texture" + "_sampler".
            // Otherwise the condition is  Sampler.Name == "g_Texture_sampler" + "".
            const auto* CombinedSamplerSuffix = Resources.GetCombinedSamplerSuffix();
            for (Uint32 n=0; n < Resources.GetNumSamplers() && !StaticSamplerFound; ++n)
            {
                const auto& Sampler = Resources.GetSampler(n);
                StaticSamplerFound = StreqSuff(Sampler.Name, TexOrSamName, CombinedSamplerSuffix);
            }
        }

        if (!StaticSamplerFound)
        {
            LOG_WARNING_MESSAGE("Static sampler '", TexOrSamName, "' is not found in any of the designated shader stages: ",
                                GetAllowedShadersString(StSamDesc.ShaderStages));
        }
    }
}
#endif


Uint32 ShaderResources::FindAssignedSamplerId(const D3DShaderResourceAttribs& TexSRV, const char* SamplerSuffix)const
{
    VERIFY_EXPR(SamplerSuffix != nullptr && *SamplerSuffix != 0);
    VERIFY_EXPR(TexSRV.GetInputType() == D3D_SIT_TEXTURE && TexSRV.GetSRVDimension() != D3D_SRV_DIMENSION_BUFFER);
    auto NumSamplers = GetNumSamplers();
    for (Uint32 s = 0; s < NumSamplers; ++s)
    {
        const auto& Sampler = GetSampler(s);
        if ( StreqSuff(Sampler.Name, TexSRV.Name, SamplerSuffix) )
        {
            DEV_CHECK_ERR(Sampler.BindCount == TexSRV.BindCount || Sampler.BindCount == 1, "Sampler '", Sampler.Name, "' assigned to texture '", TexSRV.Name, "' must be scalar or have the same array dimension (", TexSRV.BindCount, "). Actual sampler array dimension : ",  Sampler.BindCount);
            return s;
        }
    }
    return D3DShaderResourceAttribs::InvalidSamplerId;
}

bool ShaderResources::IsCompatibleWith(const ShaderResources &Res)const
{
    if (GetNumCBs()      != Res.GetNumCBs()    ||
        GetNumTexSRV()   != Res.GetNumTexSRV() ||
        GetNumTexUAV()   != Res.GetNumTexUAV() ||
        GetNumBufSRV()   != Res.GetNumBufSRV() ||
        GetNumBufUAV()   != Res.GetNumBufUAV() ||
        GetNumSamplers() != Res.GetNumSamplers())
        return false;

    bool IsCompatible = true;
    ProcessResources(
        [&](const D3DShaderResourceAttribs& CB, Uint32 n)
        {
            if (!CB.IsCompatibleWith(Res.GetCB(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& Sam, Uint32 n)
        {
            if (!Sam.IsCompatibleWith(Res.GetSampler(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& TexSRV, Uint32 n)
        {
            if (!TexSRV.IsCompatibleWith(Res.GetTexSRV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& TexUAV, Uint32 n)
        {
            if (!TexUAV.IsCompatibleWith(Res.GetTexUAV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& BufSRV, Uint32 n)
        {
            if (!BufSRV.IsCompatibleWith(Res.GetBufSRV(n)))
                IsCompatible = false;
        },
        [&](const D3DShaderResourceAttribs& BufUAV, Uint32 n)
        {
            if (!BufUAV.IsCompatibleWith(Res.GetBufUAV(n)))
                IsCompatible = false;
        }
    );
    return IsCompatible;
}

HLSLShaderResourceDesc D3DShaderResourceAttribs::GetHLSLResourceDesc()const
{
    HLSLShaderResourceDesc ResourceDesc;
    ResourceDesc.Name           = Name;
    ResourceDesc.ArraySize      = BindCount;
    ResourceDesc.ShaderRegister = BindPoint;
    switch(GetInputType())
    {
        case D3D_SIT_CBUFFER:
            ResourceDesc.Type = SHADER_RESOURCE_TYPE_CONSTANT_BUFFER;
        break;

        case D3D_SIT_TBUFFER:
            UNSUPPORTED( "TBuffers are not supported" ); 
            ResourceDesc.Type = SHADER_RESOURCE_TYPE_UNKNOWN;
        break;

        case D3D_SIT_TEXTURE:
            ResourceDesc.Type = (GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER ? SHADER_RESOURCE_TYPE_BUFFER_SRV : SHADER_RESOURCE_TYPE_TEXTURE_SRV);
        break;

        case D3D_SIT_SAMPLER:
            ResourceDesc.Type = SHADER_RESOURCE_TYPE_SAMPLER;
        break;

        case D3D_SIT_UAV_RWTYPED:
            ResourceDesc.Type = (GetSRVDimension() == D3D_SRV_DIMENSION_BUFFER ? SHADER_RESOURCE_TYPE_BUFFER_UAV : SHADER_RESOURCE_TYPE_TEXTURE_UAV);
        break;

        case D3D_SIT_STRUCTURED:
        case D3D_SIT_BYTEADDRESS:
            ResourceDesc.Type = SHADER_RESOURCE_TYPE_BUFFER_SRV;
        break;

        case D3D_SIT_UAV_RWSTRUCTURED:
        case D3D_SIT_UAV_RWBYTEADDRESS:
        case D3D_SIT_UAV_APPEND_STRUCTURED:
        case D3D_SIT_UAV_CONSUME_STRUCTURED:
        case D3D_SIT_UAV_RWSTRUCTURED_WITH_COUNTER:
            ResourceDesc.Type = SHADER_RESOURCE_TYPE_BUFFER_UAV;
        break;

        default:
            UNEXPECTED("Unknown input type");
    }

    return ResourceDesc;
}

HLSLShaderResourceDesc ShaderResources::GetHLSLShaderResourceDesc(Uint32 Index)const
{
    DEV_CHECK_ERR(Index < m_TotalResources, "Resource index (", Index, ") is out of range");
    HLSLShaderResourceDesc HLSLResourceDesc = {};
    if (Index < m_TotalResources)
    {
        const auto& Res = GetResAttribs(Index, m_TotalResources, 0);
        return Res.GetHLSLResourceDesc();
    }
    return HLSLResourceDesc;
}

size_t ShaderResources::GetHash()const
{
    size_t hash = ComputeHash(GetNumCBs(), GetNumTexSRV(), GetNumTexUAV(), GetNumBufSRV(), GetNumBufUAV(), GetNumSamplers());
    for (Uint32 n=0; n < m_TotalResources; ++n)
    {
        const auto& Res = GetResAttribs(n, m_TotalResources, 0);
        HashCombine(hash, Res);
    }

    return hash;
}

}
