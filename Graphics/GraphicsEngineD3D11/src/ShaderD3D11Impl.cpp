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

#include "ShaderD3D11Impl.h"
#include "RenderDeviceD3D11Impl.h"
#include "ResourceMapping.h"

namespace Diligent
{

static const std::string HLSLVersionToShaderModelString(const ShaderCreateInfo::ShaderVersion& Version, Uint8 MaxMajorRevision, Uint8 MaxMinorRevision)
{
    std::string ModelStr;
    if (Version.Major > MaxMajorRevision || Version.Major == MaxMajorRevision && Version.Minor > MaxMinorRevision)
    {
        ModelStr = std::to_string(Uint32{MaxMajorRevision}) + '_' + std::to_string(Uint32{MaxMinorRevision});
        LOG_ERROR_MESSAGE("Shader model ", Uint32{Version.Major}, "_", Uint32{Version.Minor}, " is not supported by this device. "
                          "Maximum supported model: ", ModelStr, ". Attempting to use ", ModelStr, '.');
    }
    else
    {
        ModelStr = std::to_string(Uint32{Version.Major}) + '_' + std::to_string(Uint32{Version.Minor});
    }
    return ModelStr;
}

static const std::string GetD3D11ShaderModel(ID3D11Device* pd3d11Device, const ShaderCreateInfo::ShaderVersion& HLSLVersion)
{
    auto d3dDeviceFeatureLevel = pd3d11Device->GetFeatureLevel();
    switch(d3dDeviceFeatureLevel)
    {
        // Direct3D11 only supports shader model 5.0 even if the device feature level is
        // above 11.0 (for example, 11.1 or 12.0).
        // https://docs.microsoft.com/en-us/windows/win32/direct3d11/overviews-direct3d-11-devices-downlevel-intro#overview-for-each-feature-level
#if defined(_WIN32_WINNT_WIN10) && (_WIN32_WINNT >=_WIN32_WINNT_WIN10)
        case D3D_FEATURE_LEVEL_12_1:
        case D3D_FEATURE_LEVEL_12_0:
#endif
        case D3D_FEATURE_LEVEL_11_1:
        case D3D_FEATURE_LEVEL_11_0:
            return (HLSLVersion.Major == 0 && HLSLVersion.Minor == 0) ?
                    std::string{"5_0"} : HLSLVersionToShaderModelString(HLSLVersion, 5, 0);

        case D3D_FEATURE_LEVEL_10_1:
            return (HLSLVersion.Major == 0 && HLSLVersion.Minor == 0) ? 
                    std::string{"4_1"} : HLSLVersionToShaderModelString(HLSLVersion, 4, 1);

        case D3D_FEATURE_LEVEL_10_0:
            return (HLSLVersion.Major == 0 && HLSLVersion.Minor == 0) ? 
                    std::string{"4_0"} : HLSLVersionToShaderModelString(HLSLVersion, 4, 0);

        default:
            UNEXPECTED("Unexpected D3D feature level ", static_cast<Uint32>(d3dDeviceFeatureLevel));
            return "4_0";
    }
}

ShaderD3D11Impl::ShaderD3D11Impl(IReferenceCounters*          pRefCounters,
                                 RenderDeviceD3D11Impl*       pRenderDeviceD3D11,
                                 const ShaderCreateInfo&      ShaderCI) : 
    TShaderBase
    {
        pRefCounters,
        pRenderDeviceD3D11,
        ShaderCI.Desc
    },
    ShaderD3DBase{ShaderCI, GetD3D11ShaderModel(pRenderDeviceD3D11->GetD3D11Device(), ShaderCI.HLSLVersion).c_str()}
{
    auto *pDeviceD3D11 = pRenderDeviceD3D11->GetD3D11Device();
    switch (ShaderCI.Desc.ShaderType)
    {

#define CREATE_SHADER(SHADER_NAME, ShaderName)\
        case SHADER_TYPE_##SHADER_NAME:         \
        {                                       \
            ID3D11##ShaderName##Shader *pShader;    \
            HRESULT hr = pDeviceD3D11->Create##ShaderName##Shader( m_pShaderByteCode->GetBufferPointer(), m_pShaderByteCode->GetBufferSize(), NULL, &pShader ); \
            CHECK_D3D_RESULT_THROW( hr, "Failed to create D3D11 shader" );      \
            if( SUCCEEDED(hr) )                     \
            {                                       \
                pShader->QueryInterface( __uuidof(ID3D11DeviceChild), reinterpret_cast<void**>( static_cast<ID3D11DeviceChild**>(&m_pShader) ) ); \
                pShader->Release();                 \
            }                                       \
            break;                                  \
        }

        CREATE_SHADER(VERTEX,   Vertex)
        CREATE_SHADER(PIXEL,    Pixel)
        CREATE_SHADER(GEOMETRY, Geometry)
        CREATE_SHADER(DOMAIN,   Domain)
        CREATE_SHADER(HULL,     Hull)
        CREATE_SHADER(COMPUTE,  Compute)

        default: UNEXPECTED( "Unknown shader type" );
    }
    
    if(!m_pShader)
        LOG_ERROR_AND_THROW("Failed to create the shader from the byte code");

    if (*m_Desc.Name != 0)
    {
        auto hr = m_pShader->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(strlen(m_Desc.Name)), m_Desc.Name);
        DEV_CHECK_ERR(SUCCEEDED(hr), "Failed to set shader name");
    }

    // Load shader resources
    auto& Allocator = GetRawAllocator();
    auto* pRawMem = ALLOCATE(Allocator, "Allocator for ShaderResources", ShaderResourcesD3D11, 1);
    auto* pResources = new (pRawMem) ShaderResourcesD3D11(pRenderDeviceD3D11, m_pShaderByteCode, m_Desc, ShaderCI.UseCombinedTextureSamplers ? ShaderCI.CombinedSamplerSuffix : nullptr);
    m_pShaderResources.reset(pResources, STDDeleterRawMem<ShaderResourcesD3D11>(Allocator));

    // Byte code is only required for the vertex shader to create input layout
    if( ShaderCI.Desc.ShaderType != SHADER_TYPE_VERTEX )
        m_pShaderByteCode.Release();
}

ShaderD3D11Impl::~ShaderD3D11Impl()
{
}

void ShaderD3D11Impl::QueryInterface(const INTERFACE_ID& IID, IObject** ppInterface)
{
    if (ppInterface == nullptr)
        return;
    if (IID == IID_ShaderD3D || IID == IID_ShaderD3D11)
    {
        *ppInterface = this;
        (*ppInterface)->AddRef();
    }
    else
    {
        TShaderBase::QueryInterface( IID, ppInterface );
    }
}

}
