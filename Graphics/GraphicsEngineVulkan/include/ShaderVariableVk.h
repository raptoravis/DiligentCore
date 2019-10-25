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
/// Declaration of Diligent::ShaderVariableManagerVk and Diligent::ShaderVariableVkImpl classes

// 
//  * ShaderVariableManagerVk keeps list of variables of specific types
//  * Every ShaderVariableVkImpl references VkResource from ShaderResourceLayoutVk
//  * ShaderVariableManagerVk keeps reference to ShaderResourceCacheVk
//  * ShaderVariableManagerVk is used by PipelineStateVkImpl to manage static resources and by
//    ShaderResourceBindingVkImpl to manage mutable and dynamic resources
//
//          __________________________                   __________________________________________________________________________
//         |                          |                 |                           |                            |                 |
//    .----|  ShaderVariableManagerVk |---------------->|  ShaderVariableVkImpl[0]  |   ShaderVariableVkImpl[1]  |     ...         |
//    |    |__________________________|                 |___________________________|____________________________|_________________|
//    |                                                                     \                          |
//    |                                                                     Ref                       Ref
//    |                                                                       \                        |
//    |     ___________________________                  ______________________V_______________________V____________________________
//    |    |                           |   unique_ptr   |                   |                 |               |                     |
//    |    | ShaderResourceLayoutVk    |--------------->|   VkResource[0]   |  VkResource[1]  |       ...     | VkResource[s+m+d-1] |
//    |    |___________________________|                |___________________|_________________|_______________|_____________________|
//    |                                                        |                                                            |
//    |                                                        |                                                            |
//    |                                                        | (DescriptorSet, CacheOffset)                              / (DescriptorSet, CacheOffset)
//    |                                                         \                                                         /
//    |     __________________________                   ________V_______________________________________________________V_______
//    |    |                          |                 |                                                                        |
//    '--->|   ShaderResourceCacheVk  |---------------->|                                   Resources                            |
//         |__________________________|                 |________________________________________________________________________|
//
//

#include <memory>

#include "ShaderResourceLayoutVk.h"
#include "ShaderResourceVariableBase.h"

namespace Diligent
{

class ShaderVariableVkImpl;

// sizeof(ShaderVariableManagerVk) == 32 (x64, msvc, Release)
class ShaderVariableManagerVk
{
public:
    ShaderVariableManagerVk(IObject&                              Owner,
                            const ShaderResourceLayoutVk&         SrcLayout, 
                            IMemoryAllocator&                     Allocator,
                            const SHADER_RESOURCE_VARIABLE_TYPE*  AllowedVarTypes, 
                            Uint32                                NumAllowedTypes, 
                            ShaderResourceCacheVk&                ResourceCache);

    ~ShaderVariableManagerVk();

    void DestroyVariables(IMemoryAllocator& Allocator);

    ShaderVariableVkImpl* GetVariable(const Char* Name);
    ShaderVariableVkImpl* GetVariable(Uint32 Index);

    void BindResources(IResourceMapping* pResourceMapping, Uint32 Flags);

    static size_t GetRequiredMemorySize(const ShaderResourceLayoutVk&          Layout, 
                                        const SHADER_RESOURCE_VARIABLE_TYPE*   AllowedVarTypes, 
                                        Uint32                                 NumAllowedTypes,
                                        Uint32&                                NumVariables);

    Uint32 GetVariableCount()const { return m_NumVariables; }

private:
    friend ShaderVariableVkImpl;

    Uint32 GetVariableIndex(const ShaderVariableVkImpl& Variable);

    IObject&                      m_Owner;
    // Variable mgr is owned by either Pipeline state object (in which case m_ResourceCache references
    // static resource cache owned by the same PSO object), or by SRB object (in which case 
    // m_ResourceCache references the cache in the SRB). Thus the cache and the resource layout
    // (which the variables reference) are guaranteed to be alive while the manager is alive.
    ShaderResourceCacheVk&        m_ResourceCache;

    // Memory is allocated through the allocator provided by the pipeline state. If allocation granularity > 1, fixed block
    // memory allocator is used. This ensures that all resources from different shader resource bindings reside in
    // continuous memory. If allocation granularity == 1, raw allocator is used.
    ShaderVariableVkImpl*         m_pVariables     = nullptr;
    Uint32                        m_NumVariables = 0;

#ifdef _DEBUG
    IMemoryAllocator&             m_DbgAllocator;
#endif
};

// sizeof(ShaderVariableVkImpl) == 24 (x64)
class ShaderVariableVkImpl final : public IShaderResourceVariable
{
public:
    ShaderVariableVkImpl(ShaderVariableManagerVk&                  ParentManager,
                         const ShaderResourceLayoutVk::VkResource& Resource) :
        m_ParentManager(ParentManager),
        m_Resource(Resource)
    {}

    ShaderVariableVkImpl            (const ShaderVariableVkImpl&) = delete;
    ShaderVariableVkImpl            (ShaderVariableVkImpl&&)      = delete;
    ShaderVariableVkImpl& operator= (const ShaderVariableVkImpl&) = delete;
    ShaderVariableVkImpl& operator= (ShaderVariableVkImpl&&)      = delete;


    virtual IReferenceCounters* GetReferenceCounters()const override final
    {
        return m_ParentManager.m_Owner.GetReferenceCounters();
    }

    virtual Atomics::Long AddRef()override final
    {
        return m_ParentManager.m_Owner.AddRef();
    }

    virtual Atomics::Long Release()override final
    {
        return m_ParentManager.m_Owner.Release();
    }

    void QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface)override final
    {
        if (ppInterface == nullptr)
            return;

        *ppInterface = nullptr;
        if (IID == IID_ShaderResourceVariable || IID == IID_Unknown)
        {
            *ppInterface = this;
            (*ppInterface)->AddRef();
        }
    }

    virtual SHADER_RESOURCE_VARIABLE_TYPE GetType()const override final
    {
        return m_Resource.GetVariableType();
    }

    virtual void Set(IDeviceObject* pObject)override final 
    {
        m_Resource.BindResource(pObject, 0, m_ParentManager.m_ResourceCache); 
    }

    virtual void SetArray(IDeviceObject* const* ppObjects, Uint32 FirstElement, Uint32 NumElements)override final
    {
        VerifyAndCorrectSetArrayArguments(m_Resource.SpirvAttribs.Name, m_Resource.SpirvAttribs.ArraySize, FirstElement, NumElements);
        for (Uint32 Elem = 0; Elem < NumElements; ++Elem)
            m_Resource.BindResource(ppObjects[Elem], FirstElement + Elem, m_ParentManager.m_ResourceCache);
    }

    virtual ShaderResourceDesc GetResourceDesc()const override final
    {
        return m_Resource.SpirvAttribs.GetResourceDesc();
    }

    virtual Uint32 GetIndex()const override final
    {
        return m_ParentManager.GetVariableIndex(*this);
    }

    virtual bool IsBound(Uint32 ArrayIndex) const override final
    {
        return m_Resource.IsBound(ArrayIndex, m_ParentManager.m_ResourceCache);
    }

    const ShaderResourceLayoutVk::VkResource& GetResource()const
    {
        return m_Resource;
    }

private:
    friend ShaderVariableManagerVk;

    ShaderVariableManagerVk&                  m_ParentManager;
    const ShaderResourceLayoutVk::VkResource& m_Resource;
};

}
