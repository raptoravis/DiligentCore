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
/// Definition of the Diligent::ISampler interface and related data structures

#include <cfloat>
#include "DeviceObject.h"

namespace Diligent
{

// {595A59BF-FA81-4855-BC5E-C0E048745A95}
static constexpr INTERFACE_ID IID_Sampler =
{ 0x595a59bf, 0xfa81, 0x4855, { 0xbc, 0x5e, 0xc0, 0xe0, 0x48, 0x74, 0x5a, 0x95 } };

/// Sampler description

/// This structure describes the sampler state which is used in a call to 
/// IRenderDevice::CreateSampler() to create a sampler object.
///
/// To create an anisotropic filter, all three filters must either be Diligent::FILTER_TYPE_ANISOTROPIC
/// or Diligent::FILTER_TYPE_COMPARISON_ANISOTROPIC.
///
/// MipFilter cannot be comparison filter except for Diligent::FILTER_TYPE_ANISOTROPIC if all 
/// three filters have that value.
///
/// Both MinFilter and MagFilter must either be regular filters or comparison filters.
/// Mixing comparison and regular filters is an error.
struct SamplerDesc : DeviceObjectAttribs
{
    /// Texture minification filter, see Diligent::FILTER_TYPE for details.
    /// Default value: Diligent::FILTER_TYPE_LINEAR.
    FILTER_TYPE MinFilter               = FILTER_TYPE_LINEAR;
    
    /// Texture magnification filter, see Diligent::FILTER_TYPE for details.
    /// Default value: Diligent::FILTER_TYPE_LINEAR.
    FILTER_TYPE MagFilter               = FILTER_TYPE_LINEAR;

    /// Mip filter, see Diligent::FILTER_TYPE for details. 
    /// Only FILTER_TYPE_POINT, FILTER_TYPE_LINEAR, FILTER_TYPE_ANISOTROPIC, and 
    /// FILTER_TYPE_COMPARISON_ANISOTROPIC are allowed.
    /// Default value: Diligent::FILTER_TYPE_LINEAR.
    FILTER_TYPE MipFilter               = FILTER_TYPE_LINEAR;

    /// Texture address mode for U coordinate, see Diligent::TEXTURE_ADDRESS_MODE for details
    /// Default value: Diligent::TEXTURE_ADDRESS_CLAMP.
    TEXTURE_ADDRESS_MODE AddressU       = TEXTURE_ADDRESS_CLAMP;
    
    /// Texture address mode for V coordinate, see Diligent::TEXTURE_ADDRESS_MODE for details
    /// Default value: Diligent::TEXTURE_ADDRESS_CLAMP.
    TEXTURE_ADDRESS_MODE AddressV       = TEXTURE_ADDRESS_CLAMP;

    /// Texture address mode for W coordinate, see Diligent::TEXTURE_ADDRESS_MODE for details
    /// Default value: Diligent::TEXTURE_ADDRESS_CLAMP.
    TEXTURE_ADDRESS_MODE AddressW       = TEXTURE_ADDRESS_CLAMP;

    /// Offset from the calculated mipmap level. For example, if a sampler calculates that a texture 
    /// should be sampled at mipmap level 1.2 and MipLODBias is 2.3, then the texture will be sampled at 
    /// mipmap level 3.5. Default value: 0.
    Float32 MipLODBias                  = 0;

    /// Maximum anisotropy level for the anisotropic filter. Default value: 0.
    Uint32 MaxAnisotropy                = 0;

    /// A function that compares sampled data against existing sampled data when comparsion
    /// filter is used. Default value: Diligent::COMPARISON_FUNC_NEVER.
    COMPARISON_FUNCTION ComparisonFunc  = COMPARISON_FUNC_NEVER;

    /// Border color to use if TEXTURE_ADDRESS_BORDER is specified for AddressU, AddressV, or AddressW. 
    /// Default value: {0,0,0,0}
    Float32 BorderColor[4]              = {0, 0, 0, 0};

    /// Specifies the minimum value that LOD is clamped to before accessing the texture MIP levels.
    /// Must be less than or equal to MaxLOD.
    /// Default value: 0.
    float MinLOD                        = 0;

    /// Specifies the maximum value that LOD is clamped to before accessing the texture MIP levels.
    /// Must be greater than or equal to MinLOD.
    /// Default value: +FLT_MAX.
    float MaxLOD                        = +3.402823466e+38F;

    SamplerDesc()noexcept{}
     
    SamplerDesc(FILTER_TYPE          _MinFilter,
                FILTER_TYPE          _MagFilter,
                FILTER_TYPE          _MipFilter,
                TEXTURE_ADDRESS_MODE _AddressU       = SamplerDesc{}.AddressU,
                TEXTURE_ADDRESS_MODE _AddressV       = SamplerDesc{}.AddressV,
                TEXTURE_ADDRESS_MODE _AddressW       = SamplerDesc{}.AddressW,
                Float32              _MipLODBias     = SamplerDesc{}.MipLODBias,
                Uint32               _MaxAnisotropy  = SamplerDesc{}.MaxAnisotropy,
                COMPARISON_FUNCTION  _ComparisonFunc = SamplerDesc{}.ComparisonFunc,
                float                _MinLOD         = SamplerDesc{}.MinLOD,
                float                _MaxLOD         = SamplerDesc{}.MaxLOD) : 
        MinFilter      (_MinFilter),
        MagFilter      (_MagFilter),
        MipFilter      (_MipFilter),
        AddressU       (_AddressU),
        AddressV       (_AddressV),
        AddressW       (_AddressW),
        MipLODBias     (_MipLODBias),
        MaxAnisotropy  (_MaxAnisotropy),
        ComparisonFunc (_ComparisonFunc),
        MinLOD         (_MinLOD),
        MaxLOD         (_MaxLOD)
    {
        BorderColor[0] = BorderColor[1] = BorderColor[2] = BorderColor[3] = 0;
    }
    /// Tests if two structures are equivalent

    /// \param [in] RHS - reference to the structure to perform comparison with
    /// \return 
    /// - True if all members of the two structures are equal.
    /// - False otherwise.
    /// The operator ignores DeviceObjectAttribs::Name field as it does not affect 
    /// the sampler state.
    bool operator == (const SamplerDesc& RHS)const
    {
                // Name is primarily used for debug purposes and does not affect the state.
                // It is ignored in comparison operation.
        return  // strcmp(Name, RHS.Name) == 0          &&
                MinFilter       == RHS.MinFilter      &&
                MagFilter       == RHS.MagFilter      && 
                MipFilter       == RHS.MipFilter      && 
                AddressU        == RHS.AddressU       && 
                AddressV        == RHS.AddressV       && 
                AddressW        == RHS.AddressW       && 
                MipLODBias      == RHS.MipLODBias     && 
                MaxAnisotropy   == RHS.MaxAnisotropy  && 
                ComparisonFunc  == RHS.ComparisonFunc && 
                BorderColor[0]  == RHS.BorderColor[0] && 
                BorderColor[1]  == RHS.BorderColor[1] &&
                BorderColor[2]  == RHS.BorderColor[2] &&
                BorderColor[3]  == RHS.BorderColor[3] &&
                MinLOD          == RHS.MinLOD         && 
                MaxLOD          == RHS.MaxLOD;
    }
};

/// Texture sampler interface.

/// The interface holds the sampler state that can be used to perform texture filtering.
/// To create a sampler, call IRenderDevice::CreateSampler(). To use a sampler,
/// call ITextureView::SetSampler().
class ISampler : public IDeviceObject
{
public:
    /// Queries the specific interface, see IObject::QueryInterface() for details
    virtual void QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface)override = 0;

    /// Returns the sampler description used to create the object
    virtual const SamplerDesc& GetDesc()const override = 0;
};

}
