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
/// Declaration of Diligent::FenceD3D12Impl class

#include "FenceD3D12.h"
#include "RenderDeviceD3D12.h"
#include "FenceBase.h"
#include "RenderDeviceD3D12Impl.h"

namespace Diligent
{

class FixedBlockMemoryAllocator;

/// Implementation of the Diligent::IFenceD3D12 interface
class FenceD3D12Impl final : public FenceBase<IFenceD3D12, RenderDeviceD3D12Impl>
{
public:
    using TFenceBase = FenceBase<IFenceD3D12, RenderDeviceD3D12Impl>;

    FenceD3D12Impl(IReferenceCounters*     pRefCounters,
                   RenderDeviceD3D12Impl*  pDevice,
                   const FenceDesc&        Desc);
    ~FenceD3D12Impl();

    virtual Uint64 GetCompletedValue()override final;

    /// Resets the fence to the specified value. 
    virtual void Reset(Uint64 Value)override final;

    ID3D12Fence* GetD3D12Fence()override final{ return m_pd3d12Fence; }

    virtual void WaitForCompletion(Uint64 Value)override final;

private:
    CComPtr<ID3D12Fence> m_pd3d12Fence; ///< D3D12 Fence object
};

}
