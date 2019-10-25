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
#include "ShaderResourceBindingD3D12Impl.h"
#include "PipelineStateD3D12Impl.h"
#include "ShaderD3D12Impl.h"
#include "RenderDeviceD3D12Impl.h"

namespace Diligent
{

ShaderResourceBindingD3D12Impl::ShaderResourceBindingD3D12Impl(IReferenceCounters*     pRefCounters,
                                                               PipelineStateD3D12Impl* pPSO,
                                                               bool                    IsPSOInternal) :
    TBase
    {
        pRefCounters,
        pPSO,
        IsPSOInternal
    },
    m_ShaderResourceCache{ShaderResourceCacheD3D12::DbgCacheContentType::SRBResources},
    m_NumShaders         {static_cast<decltype(m_NumShaders)>(pPSO->GetNumShaders())}
{
    auto* ppShaders = pPSO->GetShaders();

    auto* pRenderDeviceD3D12Impl = ValidatedCast<RenderDeviceD3D12Impl>(pPSO->GetDevice());
    auto& ResCacheDataAllocator = pPSO->GetSRBMemoryAllocator().GetResourceCacheDataAllocator(0);
    pPSO->GetRootSignature().InitResourceCache(pRenderDeviceD3D12Impl, m_ShaderResourceCache, ResCacheDataAllocator);
    
    m_pShaderVarMgrs = ALLOCATE(GetRawAllocator(), "Raw memory for ShaderVariableManagerD3D12", ShaderVariableManagerD3D12, m_NumShaders);

    for (Uint32 s = 0; s < m_NumShaders; ++s)
    {
        auto* pShader = ppShaders[s];
        auto ShaderType = pShader->GetDesc().ShaderType;
        auto ShaderInd = GetShaderTypeIndex(ShaderType);

        auto& VarDataAllocator = pPSO->GetSRBMemoryAllocator().GetShaderVariableDataAllocator(s);

        // http://diligentgraphics.com/diligent-engine/architecture/d3d12/shader-resource-layout#Initializing-Resource-Layouts-in-a-Shader-Resource-Binding-Object
        const SHADER_RESOURCE_VARIABLE_TYPE AllowedVarTypes[] = { SHADER_RESOURCE_VARIABLE_TYPE_MUTABLE, SHADER_RESOURCE_VARIABLE_TYPE_DYNAMIC };
        const auto& SrcLayout = pPSO->GetShaderResLayout(s);
        // Create shader variable manager in place
        new (m_pShaderVarMgrs + s)
            ShaderVariableManagerD3D12
            {
                *this,
                SrcLayout,
                VarDataAllocator,
                AllowedVarTypes,
                _countof(AllowedVarTypes),
                m_ShaderResourceCache
            };

        m_ResourceLayoutIndex[ShaderInd] = static_cast<Int8>(s);
    }
}

ShaderResourceBindingD3D12Impl::~ShaderResourceBindingD3D12Impl()
{
    auto* pPSO = ValidatedCast<PipelineStateD3D12Impl>(m_pPSO);
    for(Uint32 s = 0; s < m_NumShaders; ++s)
    {
        auto &VarDataAllocator = pPSO->GetSRBMemoryAllocator().GetShaderVariableDataAllocator(s);
        m_pShaderVarMgrs[s].Destroy(VarDataAllocator);
        m_pShaderVarMgrs[s].~ShaderVariableManagerD3D12();
    }

    GetRawAllocator().Free(m_pShaderVarMgrs);
}

IMPLEMENT_QUERY_INTERFACE( ShaderResourceBindingD3D12Impl, IID_ShaderResourceBindingD3D12, TBase )

void ShaderResourceBindingD3D12Impl::BindResources(Uint32 ShaderFlags, IResourceMapping* pResMapping, Uint32 Flags)
{
    for (auto ShaderInd = 0; ShaderInd <= CSInd; ++ShaderInd )
    {
        if (ShaderFlags & GetShaderTypeFromIndex(ShaderInd))
        {
            auto ResLayoutInd = m_ResourceLayoutIndex[ShaderInd];
            if(ResLayoutInd >= 0)
            {
                m_pShaderVarMgrs[ResLayoutInd].BindResources(pResMapping, Flags);
            }
        }
    }
}

IShaderResourceVariable* ShaderResourceBindingD3D12Impl::GetVariableByName(SHADER_TYPE ShaderType, const char* Name)
{
    auto ShaderInd = GetShaderTypeIndex(ShaderType);
    auto ResLayoutInd = m_ResourceLayoutIndex[ShaderInd];
    if (ResLayoutInd < 0)
    {
        LOG_WARNING_MESSAGE("Unable to find mutable/dynamic variable '", Name, "': shader stage ", GetShaderTypeLiteralName(ShaderType),
                            " is inactive in Pipeline State '", m_pPSO->GetDesc().Name, "'");
        return nullptr;
    }
    return m_pShaderVarMgrs[ResLayoutInd].GetVariable(Name);
}

Uint32 ShaderResourceBindingD3D12Impl::GetVariableCount(SHADER_TYPE ShaderType) const 
{
    auto ShaderInd = GetShaderTypeIndex(ShaderType);
    auto ResLayoutInd = m_ResourceLayoutIndex[ShaderInd];
    if (ResLayoutInd < 0)
    {
        LOG_WARNING_MESSAGE("Unable to get the number of mutable/dynamic variables: shader stage ", GetShaderTypeLiteralName(ShaderType),
                            " is inactive in Pipeline State '", m_pPSO->GetDesc().Name, "'");
        return 0;
    }

    return m_pShaderVarMgrs[ResLayoutInd].GetVariableCount();
}

IShaderResourceVariable* ShaderResourceBindingD3D12Impl::GetVariableByIndex(SHADER_TYPE ShaderType, Uint32 Index)
{
    auto ShaderInd = GetShaderTypeIndex(ShaderType);
    auto ResLayoutInd = m_ResourceLayoutIndex[ShaderInd];
    if (ResLayoutInd < 0)
    {
        LOG_WARNING_MESSAGE("Unable to get mutable/dynamic variable at index ", Index, ": shader stage ", GetShaderTypeLiteralName(ShaderType),
                            " is inactive in Pipeline State '", m_pPSO->GetDesc().Name, "'");
        return nullptr;
    }

    return m_pShaderVarMgrs[ResLayoutInd].GetVariable(Index);
}


#ifdef DEVELOPMENT
void ShaderResourceBindingD3D12Impl::dvpVerifyResourceBindings(const PipelineStateD3D12Impl* pPSO)const
{
    auto* pRefPSO = GetPipelineState<const PipelineStateD3D12Impl>();
    if (pPSO->IsIncompatibleWith(pRefPSO))
    {
        LOG_ERROR("Shader resource binding is incompatible with the pipeline state \"", pPSO->GetDesc().Name, '\"');
        return;
    }
    for(Uint32 l = 0; l < m_NumShaders; ++l)
    {
        // Use reference layout from pipeline state that contains all shader resource types
        const auto& ShaderResLayout = pRefPSO->GetShaderResLayout(l);
        ShaderResLayout.dvpVerifyBindings(m_ShaderResourceCache);
    }
#ifdef _DEBUG
    m_ShaderResourceCache.DbgVerifyBoundDynamicCBsCounter();
#endif
}
#endif


void ShaderResourceBindingD3D12Impl::InitializeStaticResources(const IPipelineState* pPSO)
{
    if (StaticResourcesInitialized())
    {
        LOG_WARNING_MESSAGE("Static resources have already been initialized in this shader resource binding object. The operation will be ignored.");
        return;
    }

    if (pPSO == nullptr)
    {
        pPSO = GetPipelineState();
    }
    else
    {
        DEV_CHECK_ERR(pPSO->IsCompatibleWith(GetPipelineState()), "The pipeline state is not compatible with this SRB");
    }

    auto* pPSO12 = ValidatedCast<const PipelineStateD3D12Impl>(pPSO);
    auto NumShaders = pPSO12->GetNumShaders();
    // Copy static resources
    for (Uint32 s = 0; s < NumShaders; ++s)
    {
        const auto& ShaderResLayout = pPSO12->GetShaderResLayout(s);
        auto& StaticResLayout = pPSO12->GetStaticShaderResLayout(s);
        auto& StaticResCache = pPSO12->GetStaticShaderResCache(s);
#ifdef DEVELOPMENT
        if (!StaticResLayout.dvpVerifyBindings(StaticResCache))
        {
            auto* pShader = pPSO12->GetShader<ShaderD3D12Impl>(s);
            LOG_ERROR_MESSAGE("Static resources in SRB of PSO '", pPSO12->GetDesc().Name, "' will not be successfully initialized "
                              "because not all static resource bindings in shader '", pShader->GetDesc().Name, "' are valid. "
                              "Please make sure you bind all static resources to PSO before calling InitializeStaticResources() "
                              "directly or indirectly by passing InitStaticResources=true to CreateShaderResourceBinding() method.");
        }
#endif
        StaticResLayout.CopyStaticResourceDesriptorHandles(StaticResCache, ShaderResLayout, m_ShaderResourceCache);
    }

#ifdef _DEBUG
    m_ShaderResourceCache.DbgVerifyBoundDynamicCBsCounter();
#endif

    m_bStaticResourcesInitialized = true;
}

}
