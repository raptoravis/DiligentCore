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

#include "Texture1D_OGL.h"
#include "RenderDeviceGLImpl.h"
#include "DeviceContextGLImpl.h"
#include "GLTypeConversions.h"
#include "BufferGLImpl.h"

namespace Diligent
{

Texture1D_OGL::Texture1D_OGL(IReferenceCounters*         pRefCounters, 
                             FixedBlockMemoryAllocator&  TexViewObjAllocator,     
                             RenderDeviceGLImpl*         pDeviceGL, 
                             GLContextState&             GLState,
                             const TextureDesc&          TexDesc, 
                             const TextureData*          pInitData         /*= nullptr*/, 
							 bool                        bIsDeviceInternal /*= false*/) : 
    TextureBaseGL
    {
        pRefCounters,
        TexViewObjAllocator,
        pDeviceGL,
        TexDesc,
        GL_TEXTURE_1D,
        pInitData,
        bIsDeviceInternal
    }
{
    GLState.BindTexture(-1, m_BindTarget, m_GlTexture);

    //                             levels             format          width
    glTexStorage1D(m_BindTarget, m_Desc.MipLevels, m_GLTexFormat, m_Desc.Width);
    CHECK_GL_ERROR_AND_THROW("Failed to allocate storage for the 1D texture");
    // When target is GL_TEXTURE_1D, calling glTexStorage1D is equivalent to the following pseudo-code:
    //for (i = 0; i < levels; i++)
    //{
    //    glTexImage1D(target, i, internalformat, width, 0, format, type, NULL);
    //    width = max(1, (width / 2));
    //}

    SetDefaultGLParameters();

    if (pInitData != nullptr && pInitData->pSubResources != nullptr)
    {
        if (m_Desc.MipLevels == pInitData->NumSubresources)
        {
            for(Uint32 Mip = 0; Mip < m_Desc.MipLevels; ++Mip)
            {
                Box DstBox{0, std::max(m_Desc.Width>>Mip, 1U),
                           0, 1};
                // UpdateData() is a virtual function. If we try to call it through vtbl from here,
                // we will get into TextureBaseGL::UpdateData(), because instance of Texture1D_OGL
                // is not fully constructed yet.
                // To call the required function, we need to explicitly specify the class: 
                Texture1D_OGL::UpdateData( GLState, Mip, 0, DstBox, pInitData->pSubResources[Mip] );
            }
        }
        else
        {
            UNEXPECTED( "Incorrect number of subresources" );
        }
    }

    GLState.BindTexture(-1, m_BindTarget, GLObjectWrappers::GLTextureObj::Null() );
}

Texture1D_OGL::Texture1D_OGL( IReferenceCounters*           pRefCounters, 
                              FixedBlockMemoryAllocator&    TexViewObjAllocator,     
                              RenderDeviceGLImpl*           pDeviceGL, 
                              GLContextState&               GLState,
                              const TextureDesc&            TexDesc,
                              GLuint                        GLTextureHandle,
                              bool                          bIsDeviceInternal)  : 
    TextureBaseGL(pRefCounters, TexViewObjAllocator, pDeviceGL, GLState, TexDesc, GLTextureHandle, GL_TEXTURE_1D, bIsDeviceInternal)
{
}

Texture1D_OGL::~Texture1D_OGL()
{
}

void Texture1D_OGL::UpdateData( GLContextState&           ContextState,
                                Uint32                    MipLevel,
                                Uint32                    Slice,
                                const Box&                DstBox,
                                const TextureSubResData&  SubresData )
{
    TextureBaseGL::UpdateData(ContextState, MipLevel, Slice, DstBox, SubresData);

    ContextState.BindTexture( -1, m_BindTarget, m_GlTexture );

    // Bind buffer if it is provided; copy from CPU memory otherwise
    GLuint UnpackBuffer = 0;
    if (SubresData.pSrcBuffer != nullptr)
    {
        auto *pBufferGL = ValidatedCast<BufferGLImpl>(SubresData.pSrcBuffer);
        UnpackBuffer = pBufferGL->GetGLHandle();
    }

    // Transfers to OpenGL memory are called unpack operations
    // If there is a buffer bound to GL_PIXEL_UNPACK_BUFFER target, then all the pixel transfer
    // operations will be performed from this buffer.
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER, UnpackBuffer);

    const auto &TransferAttribs = GetNativePixelTransferAttribs(m_Desc.Format);
    
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    glPixelStorei(GL_UNPACK_ROW_LENGTH, 0);
    glPixelStorei(GL_UNPACK_SKIP_PIXELS, 0);
    glPixelStorei(GL_UNPACK_SKIP_ROWS, 0 );

    glTexSubImage1D(m_BindTarget, MipLevel,
                    DstBox.MinX, 
                    DstBox.MaxX - DstBox.MinX,
                    TransferAttribs.PixelFormat, TransferAttribs.DataType, 
                    // If a non-zero named buffer object is bound to the GL_PIXEL_UNPACK_BUFFER target, 'data' is treated
                    // as a byte offset into the buffer object's data store.
                    // https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glTexSubImage1D.xml
                    SubresData.pSrcBuffer != nullptr ? reinterpret_cast<void*>(static_cast<size_t>(SubresData.SrcOffset)) : SubresData.pData);
    CHECK_GL_ERROR("Failed to update subimage data");

    if(UnpackBuffer != 0)
        glBindBuffer(GL_PIXEL_UNPACK_BUFFER, 0);

    ContextState.BindTexture( -1, m_BindTarget, GLObjectWrappers::GLTextureObj::Null() );
}

void Texture1D_OGL::AttachToFramebuffer( const TextureViewDesc& ViewDesc, GLenum AttachmentPoint )
{
    // For glFramebufferTexture1D(), if texture name is not zero, then texture target must be GL_TEXTURE_1D
    glFramebufferTexture1D( GL_DRAW_FRAMEBUFFER, AttachmentPoint, m_BindTarget, m_GlTexture, ViewDesc.MostDetailedMip );
    CHECK_GL_ERROR( "Failed to attach texture 1D to draw framebuffer" );
    glFramebufferTexture1D( GL_READ_FRAMEBUFFER, AttachmentPoint, m_BindTarget, m_GlTexture, ViewDesc.MostDetailedMip );
    CHECK_GL_ERROR( "Failed to attach texture 1D to read framebuffer" );
}

}
