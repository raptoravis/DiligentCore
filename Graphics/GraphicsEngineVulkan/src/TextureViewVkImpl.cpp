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
#include "TextureViewVkImpl.h"
#include "DeviceContextVkImpl.h"
#include "RenderDeviceVkImpl.h"

namespace Diligent
{

TextureViewVkImpl::TextureViewVkImpl( IReferenceCounters*                 pRefCounters,
                                      RenderDeviceVkImpl*                 pDevice, 
                                      const TextureViewDesc&              ViewDesc, 
                                      ITexture*                           pTexture,
                                      VulkanUtilities::ImageViewWrapper&& ImgView,
                                      bool                                bIsDefaultView) :
    TTextureViewBase
    {
        pRefCounters,
        pDevice,
        ViewDesc,
        pTexture,
        bIsDefaultView
    },
    m_ImageView{std::move(ImgView)}
{
}

TextureViewVkImpl::~TextureViewVkImpl()
{
    if (m_MipLevelViews != nullptr)
    {
        for (Uint32 MipView=0; MipView < m_Desc.NumMipLevels * 2; ++MipView)
        {
            m_MipLevelViews[MipView].~MipLevelViewAutoPtrType();
        }
        // Memory allocated in TextureVkImpl::CreateViewInternal()
        GetRawAllocator().Free(m_MipLevelViews);
    }

    if(m_Desc.ViewType == TEXTURE_VIEW_DEPTH_STENCIL || m_Desc.ViewType == TEXTURE_VIEW_RENDER_TARGET)
        m_pDevice->GetFramebufferCache().OnDestroyImageView(m_ImageView);
    m_pDevice->SafeReleaseDeviceObject(std::move(m_ImageView), m_pTexture->GetDesc().CommandQueueMask);
}

IMPLEMENT_QUERY_INTERFACE( TextureViewVkImpl, IID_TextureViewVk, TTextureViewBase )

}
