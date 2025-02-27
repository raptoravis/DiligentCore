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
/// Declaration of Diligent::FenceVkImpl class

#include <deque>
#include "FenceVk.h"
#include "FenceBase.h"
#include "VulkanUtilities/VulkanFencePool.h"
#include "RenderDeviceVkImpl.h"

namespace Diligent
{

class FixedBlockMemoryAllocator;

/// Implementation of the Diligent::IFenceVk interface
class FenceVkImpl final : public FenceBase<IFenceVk, RenderDeviceVkImpl>
{
public:
    using TFenceBase = FenceBase<IFenceVk, RenderDeviceVkImpl>;

    FenceVkImpl(IReferenceCounters* pRefCounters,
                RenderDeviceVkImpl* pRendeDeviceVkImpl,
                const FenceDesc&    Desc,
                bool                IsDeviceInternal = false);
    ~FenceVkImpl();

    // Note that this method is not thread-safe. The reason is that VulkanFencePool is not thread
    // safe, and DeviceContextVkImpl::SignalFence() adds the fence to the pending fences list that
    // are signaled later by the command context when it submits the command list. So there is no
    // guarantee that the fence pool is not accessed simultaneously by multiple threads even if the
    // fence object itself is protected by mutex.
    virtual Uint64 GetCompletedValue()override final;

    /// Resets the fence to the specified value. 
    virtual void Reset(Uint64 Value)override final;
    
    VulkanUtilities::FenceWrapper GetVkFence() { return m_FencePool.GetFence(); }
    void AddPendingFence(VulkanUtilities::FenceWrapper&& vkFence, Uint64 FenceValue)
    {
        m_PendingFences.emplace_back(FenceValue, std::move(vkFence));
    }

    void Wait(Uint64 Value);

private:
    VulkanUtilities::VulkanFencePool m_FencePool;
    std::deque<std::pair<Uint64, VulkanUtilities::FenceWrapper>> m_PendingFences;
    volatile Uint64 m_LastCompletedFenceValue = 0;
};

}
