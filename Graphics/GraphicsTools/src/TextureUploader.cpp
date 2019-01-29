/*     Copyright 2015-2019 Egor Yusov
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
#if D3D11_SUPPORTED
#include "TextureUploaderD3D11.h"
#endif

#if D3D12_SUPPORTED
#include "TextureUploaderD3D12.h"
#endif

#if GL_SUPPORTED || GLES_SUPPORTED
#include "TextureUploaderGL.h"
#endif

#if VULKAN_SUPPORTED
#include "TextureUploaderVk.h"
#endif

namespace Diligent
{
    void CreateTextureUploader(IRenderDevice *pDevice, const TextureUploaderDesc& Desc, ITextureUploader **ppUploader)
    {
        *ppUploader = nullptr;
        switch (pDevice->GetDeviceCaps().DevType)
        {
#if D3D11_SUPPORTED
            case DeviceType::D3D11:
                *ppUploader = MakeNewRCObj<TextureUploaderD3D11>()( pDevice, Desc );
                break;
#endif

#if D3D12_SUPPORTED
            case DeviceType::D3D12:
                *ppUploader = MakeNewRCObj<TextureUploaderD3D12>()( pDevice, Desc );
                break;
#endif

#if GL_SUPPORTED || GLES_SUPPORTED
            case DeviceType::OpenGLES:
            case DeviceType::OpenGL:
                *ppUploader = MakeNewRCObj<TextureUploaderGL>()( pDevice, Desc );
                break;
#endif
            
#if VULKAN_SUPPORTED
            case DeviceType::Vulkan:
                *ppUploader = MakeNewRCObj<TextureUploaderVk>()( pDevice, Desc );
                break;
#endif

            default:
                UNEXPECTED("Unexpected device type");
        }
        if (*ppUploader != nullptr)
            (*ppUploader)->AddRef();
    }
}
