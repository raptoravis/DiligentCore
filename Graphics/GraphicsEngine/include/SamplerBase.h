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
/// Implementation of the Diligent::SamplerBase template class

#include "Sampler.h"
#include "DeviceObjectBase.h"
#include "RenderDeviceBase.h"

namespace Diligent
{

/// Template class implementing base functionality for a sampler object.

/// \tparam BaseInterface - base interface that this class will inheret
///                          (Diligent::ISamplerD3D11, Diligent::ISamplerD3D12,
///                           Diligent::ISamplerGL or Diligent::ISamplerVk).
/// \tparam RenderDeviceImplType - type of the render device implementation
///                                (Diligent::RenderDeviceD3D11Impl, Diligent::RenderDeviceD3D12Impl,
///                                 Diligent::RenderDeviceGLImpl, or Diligent::RenderDeviceVkImpl)
template<class BaseInterface, class RenderDeviceImplType>
class SamplerBase : public DeviceObjectBase<BaseInterface, RenderDeviceImplType, SamplerDesc>
{
public:
    using TDeviceObjectBase = DeviceObjectBase<BaseInterface, RenderDeviceImplType, SamplerDesc>;

    /// \param pRefCounters - reference counters object that controls the lifetime of this sampler.
	/// \param pDevice - pointer to the device.
	/// \param SamDesc - sampler description.
	/// \param bIsDeviceInternal - flag indicating if the sampler is an internal device object and 
	///							   must not keep a strong reference to the device.
    SamplerBase( IReferenceCounters* pRefCounters, RenderDeviceImplType* pDevice, const SamplerDesc& SamDesc, bool bIsDeviceInternal = false ) :
        TDeviceObjectBase( pRefCounters, pDevice, SamDesc, bIsDeviceInternal )
    {}

    ~SamplerBase()
    {
        /// \note Destructor cannot directly remove the object from the registry as this may cause a 
        ///       deadlock.
        auto &SamplerRegistry = this->GetDevice()->GetSamplerRegistry();
        SamplerRegistry.ReportDeletedObject();
    }

    IMPLEMENT_QUERY_INTERFACE_IN_PLACE( IID_Sampler, TDeviceObjectBase )
};

}
