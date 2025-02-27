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
/// Definition of the Diligent::IShaderD3D12 interface

#include "../../GraphicsEngineD3DBase/interface/ShaderD3D.h"

namespace Diligent
{

// {C059B160-7F31-4029-943D-0996B98EE79A}
static constexpr INTERFACE_ID IID_ShaderD3D12 =
{ 0xc059b160, 0x7f31, 0x4029, { 0x94, 0x3d, 0x9, 0x96, 0xb9, 0x8e, 0xe7, 0x9a } };

/// Interface to the shader object implemented in D3D12
class IShaderD3D12 : public IShaderD3D
{
public:

    /// Returns a pointer to the ID3D12DeviceChild interface of the internal Direct3D12 object.

    /// The method does *NOT* call AddRef() on the returned interface,
    /// so Release() must not be called.
    //virtual ID3D12DeviceChild* GetD3D12Shader() = 0;
};

}
