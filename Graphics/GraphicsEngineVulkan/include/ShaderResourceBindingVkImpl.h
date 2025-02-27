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

#pragma once

/// \file
/// Declaration of Diligent::ShaderResourceBindingVkImpl class

#include "ShaderResourceBindingVk.h"
#include "RenderDeviceVk.h"
#include "ShaderResourceBindingBase.h"
#include "ShaderBase.h"
#include "ShaderResourceCacheVk.h"
#include "ShaderVariableVk.h"

namespace Diligent
{

class FixedBlockMemoryAllocator;

/// Implementation of the Diligent::IShaderResourceBindingVk interface
// sizeof(ShaderResourceBindingVkImpl) == 72 (x64, msvc, Release)
class ShaderResourceBindingVkImpl final : public ShaderResourceBindingBase<IShaderResourceBindingVk>
{
public:
    using TBase = ShaderResourceBindingBase<IShaderResourceBindingVk>;

    ShaderResourceBindingVkImpl(IReferenceCounters* pRefCounters, class PipelineStateVkImpl* pPSO, bool IsPSOInternal);
    ~ShaderResourceBindingVkImpl();

    virtual void QueryInterface( const INTERFACE_ID& IID, IObject** ppInterface )override final;

    virtual void BindResources(Uint32 ShaderFlags, IResourceMapping* pResMapping, Uint32 Flags)override final;

    virtual IShaderResourceVariable* GetVariableByName(SHADER_TYPE ShaderType, const char* Name)override final;

    virtual Uint32 GetVariableCount(SHADER_TYPE ShaderType) const override final;

    virtual IShaderResourceVariable* GetVariableByIndex(SHADER_TYPE ShaderType, Uint32 Index)override final;

    virtual void InitializeStaticResources(const IPipelineState* pPipelineState)override final;

    ShaderResourceCacheVk& GetResourceCache() { return m_ShaderResourceCache; }

    bool StaticResourcesInitialized() const { return m_bStaticResourcesInitialized; }

private:

    ShaderResourceCacheVk    m_ShaderResourceCache;
    ShaderVariableManagerVk* m_pShaderVarMgrs = nullptr;

    // Shader variable manager index in m_pShaderVarMgrs[] array for every shader stage
    Int8 m_ResourceLayoutIndex[6] = {-1, -1, -1, -1, -1, -1};
    bool m_bStaticResourcesInitialized = false;
    Uint8 m_NumShaders = 0;
};

}
