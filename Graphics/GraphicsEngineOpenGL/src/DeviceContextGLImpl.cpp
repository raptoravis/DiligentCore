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
#include <iostream>
#include <fstream>
#include <string>

#include "SwapChainGL.h"
#include "DeviceContextGLImpl.h"
#include "RenderDeviceGLImpl.h"
#include "GLTypeConversions.h"

#include "BufferGLImpl.h"
#include "ShaderGLImpl.h"
#include "VAOCache.h"
#include "Texture1D_OGL.h"
#include "Texture1DArray_OGL.h"
#include "Texture2D_OGL.h"
#include "Texture2DArray_OGL.h"
#include "Texture3D_OGL.h"
#include "SamplerGLImpl.h"
#include "GraphicsAccessories.h"
#include "BufferViewGLImpl.h"
#include "PipelineStateGLImpl.h"
#include "FenceGLImpl.h"
#include "ShaderResourceBindingGLImpl.h"

using namespace std;

namespace Diligent
{
    DeviceContextGLImpl::DeviceContextGLImpl(IReferenceCounters* pRefCounters, class RenderDeviceGLImpl* pDeviceGL, bool bIsDeferred) : 
        TDeviceContextBase
        {
            pRefCounters,
            pDeviceGL,
            bIsDeferred
        },
        m_ContextState                       {pDeviceGL},
        m_CommitedResourcesTentativeBarriers {0        },
        m_DefaultFBO                         {false    }
    {
        m_BoundWritableTextures.reserve( 16 );
        m_BoundWritableBuffers.reserve( 16 );
    }

    IMPLEMENT_QUERY_INTERFACE( DeviceContextGLImpl, IID_DeviceContextGL, TDeviceContextBase )


    void DeviceContextGLImpl::SetPipelineState(IPipelineState* pPipelineState)
    {
        auto* pPipelineStateGLImpl = ValidatedCast<PipelineStateGLImpl>(pPipelineState);
        if (PipelineStateGLImpl::IsSameObject(m_pPipelineState, pPipelineStateGLImpl))
            return;

        TDeviceContextBase::SetPipelineState(pPipelineStateGLImpl, 0 /*Dummy*/);

        const auto& Desc = pPipelineStateGLImpl->GetDesc();
        if (Desc.IsComputePipeline)
        {
        }
        else
        {
            const auto& GraphicsPipeline = Desc.GraphicsPipeline;
            // Set rasterizer state
            {
                const auto& RasterizerDesc = GraphicsPipeline.RasterizerDesc;

                m_ContextState.SetFillMode(RasterizerDesc.FillMode);
                m_ContextState.SetCullMode(RasterizerDesc.CullMode);
                m_ContextState.SetFrontFace(RasterizerDesc.FrontCounterClockwise);
                m_ContextState.SetDepthBias( static_cast<Float32>( RasterizerDesc.DepthBias ), RasterizerDesc.SlopeScaledDepthBias );
                if( RasterizerDesc.DepthBiasClamp != 0 )
                    LOG_WARNING_MESSAGE( "Depth bias clamp is not supported on OpenGL" );

                // Enabling depth clamping in GL is the same as disabling clipping in Direct3D.
                // https://docs.microsoft.com/en-us/windows/win32/api/d3d11/ns-d3d11-d3d11_rasterizer_desc
                // https://www.khronos.org/opengl/wiki/GLAPI/glEnable
                m_ContextState.SetDepthClamp( !RasterizerDesc.DepthClipEnable );

                m_ContextState.EnableScissorTest( RasterizerDesc.ScissorEnable );
                if( RasterizerDesc.AntialiasedLineEnable )
                    LOG_WARNING_MESSAGE( "Line antialiasing is not supported on OpenGL" );
            }

            // Set blend state
            {
                const auto& BSDsc = GraphicsPipeline.BlendDesc;
                m_ContextState.SetBlendState(BSDsc, GraphicsPipeline.SampleMask);
            }

            // Set depth-stencil state
            {
                const auto& DepthStencilDesc = GraphicsPipeline.DepthStencilDesc;

                m_ContextState.EnableDepthTest( DepthStencilDesc.DepthEnable );
                m_ContextState.EnableDepthWrites( DepthStencilDesc.DepthWriteEnable );
                m_ContextState.SetDepthFunc( DepthStencilDesc.DepthFunc );

                m_ContextState.EnableStencilTest( DepthStencilDesc.StencilEnable );

                m_ContextState.SetStencilWriteMask( DepthStencilDesc.StencilWriteMask );

                {
                    const auto& FrontFace = DepthStencilDesc.FrontFace;
                    m_ContextState.SetStencilFunc( GL_FRONT, FrontFace.StencilFunc, m_StencilRef, DepthStencilDesc.StencilReadMask );
                    m_ContextState.SetStencilOp( GL_FRONT, FrontFace.StencilFailOp, FrontFace.StencilDepthFailOp, FrontFace.StencilPassOp );
                }

                {
                    const auto& BackFace = DepthStencilDesc.BackFace;
                    m_ContextState.SetStencilFunc( GL_BACK, BackFace.StencilFunc, m_StencilRef, DepthStencilDesc.StencilReadMask );
                    m_ContextState.SetStencilOp( GL_BACK, BackFace.StencilFailOp, BackFace.StencilDepthFailOp, BackFace.StencilPassOp );
                }
            }
            m_ContextState.InvalidateVAO();
        }
    }

    void DeviceContextGLImpl::TransitionShaderResources(IPipelineState* pPipelineState, IShaderResourceBinding* pShaderResourceBinding)
    {

    }

    void DeviceContextGLImpl::CommitShaderResources(IShaderResourceBinding* pShaderResourceBinding, RESOURCE_STATE_TRANSITION_MODE StateTransitionMode, bool bCheckUAVSRV)
    {
        if (!DeviceContextBase::CommitShaderResources(pShaderResourceBinding, StateTransitionMode, 0))
            return;

        if (m_CommitedResourcesTentativeBarriers != 0)
            LOG_INFO_MESSAGE("Not all tentative resource barriers have been executed since the last call to CommitShaderResources(). Did you forget to call Draw()/DispatchCompute() ?");

        m_CommitedResourcesTentativeBarriers = 0;
        BindProgramResources(m_CommitedResourcesTentativeBarriers, pShaderResourceBinding);
        // m_CommitedResourcesTentativeBarriers will contain memory barriers that will be required 
        // AFTER the actual draw/dispatch command is executed. Before that they have no meaning
    }

    void DeviceContextGLImpl::SetStencilRef(Uint32 StencilRef)
    {
        if (TDeviceContextBase::SetStencilRef(StencilRef, 0))
        {
            m_ContextState.SetStencilRef(GL_FRONT, StencilRef);
            m_ContextState.SetStencilRef(GL_BACK, StencilRef);
        }
    }

    void DeviceContextGLImpl::SetBlendFactors(const float* pBlendFactors)
    {
        if (TDeviceContextBase::SetBlendFactors(pBlendFactors, 0))
        {
            m_ContextState.SetBlendFactors(m_BlendFactors);
        }
    }

    void DeviceContextGLImpl::SetVertexBuffers(Uint32                         StartSlot,
                                               Uint32                         NumBuffersSet,
                                               IBuffer**                      ppBuffers,
                                               Uint32*                        pOffsets,
                                               RESOURCE_STATE_TRANSITION_MODE StateTransitionMode,
                                               SET_VERTEX_BUFFERS_FLAGS       Flags)
    {
        TDeviceContextBase::SetVertexBuffers(StartSlot, NumBuffersSet, ppBuffers, pOffsets, StateTransitionMode, Flags);
        m_ContextState.InvalidateVAO();
    }

    void DeviceContextGLImpl::InvalidateState()
    {
        TDeviceContextBase::InvalidateState();

        m_ContextState.Invalidate();
        m_BoundWritableTextures.clear();
        m_BoundWritableBuffers.clear();
    }

    void DeviceContextGLImpl::SetIndexBuffer(IBuffer* pIndexBuffer, Uint32 ByteOffset, RESOURCE_STATE_TRANSITION_MODE StateTransitionMode)
    {
        TDeviceContextBase::SetIndexBuffer(pIndexBuffer, ByteOffset, StateTransitionMode);
        m_ContextState.InvalidateVAO();
    }

    void DeviceContextGLImpl::SetViewports(Uint32 NumViewports, const Viewport* pViewports, Uint32 RTWidth, Uint32 RTHeight)
    {
        TDeviceContextBase::SetViewports(NumViewports, pViewports, RTWidth, RTHeight);

        VERIFY(NumViewports == m_NumViewports, "Unexpected number of viewports");
        if (NumViewports == 1)
        {
            const auto& vp = m_Viewports[0];
            // Note that OpenGL and DirectX use different origin of 
            // the viewport in window coordinates:
            //
            // DirectX (0,0)
            //     \ ____________
            //      |            |
            //      |            |
            //      |            |
            //      |            |
            //      |____________|
            //     /
            //  OpenGL (0,0)
            //
            float BottomLeftY = static_cast<float>(RTHeight) - (vp.TopLeftY + vp.Height);
            float BottomLeftX = vp.TopLeftX;

            Int32 x = static_cast<int>(BottomLeftX);
            Int32 y = static_cast<int>(BottomLeftY);
            Int32 w = static_cast<int>(vp.Width);
            Int32 h = static_cast<int>(vp.Height);
            if( static_cast<float>(x) == BottomLeftX &&
                static_cast<float>(y) == BottomLeftY &&
                static_cast<float>(w) == vp.Width &&
                static_cast<float>(h) == vp.Height )
            {
                // GL_INVALID_VALUE is generated if either width or height is negative
                // https://www.khronos.org/registry/OpenGL-Refpages/gl2.1/xhtml/glViewport.xml
                glViewport(x, y, w, h);
            }
            else
            {
                // GL_INVALID_VALUE is generated if either width or height is negative
                // https://www.khronos.org/registry/OpenGL-Refpages/gl4/html/glViewportIndexed.xhtml
                glViewportIndexedf(0, BottomLeftX, BottomLeftY, vp.Width, vp.Height);
            }
            CHECK_GL_ERROR("Failed to set viewport");

            glDepthRangef(vp.MinDepth, vp.MaxDepth);
            CHECK_GL_ERROR("Failed to set depth range");
        }
        else
        {
            for (Uint32 i = 0; i < NumViewports; ++i)
            {
                const auto& vp = m_Viewports[i];
                float BottomLeftY = static_cast<float>(RTHeight) - (vp.TopLeftY + vp.Height);
                float BottomLeftX = vp.TopLeftX;
                glViewportIndexedf(i, BottomLeftX, BottomLeftY, vp.Width, vp.Height);
                CHECK_GL_ERROR("Failed to set viewport #", i);
                glDepthRangef(vp.MinDepth, vp.MaxDepth );
                CHECK_GL_ERROR("Failed to set depth range for viewport #", i);
            }
        }
    }

    void DeviceContextGLImpl::SetScissorRects(Uint32 NumRects, const Rect* pRects, Uint32 RTWidth, Uint32 RTHeight)
    {
        TDeviceContextBase::SetScissorRects(NumRects, pRects, RTWidth, RTHeight);

        VERIFY(NumRects == m_NumScissorRects, "Unexpected number of scissor rects");
        if (NumRects == 1)
        {
            const auto& Rect = m_ScissorRects[0];
            // Note that OpenGL and DirectX use different origin 
            // of the viewport in window coordinates:
            //
            // DirectX (0,0)
            //     \ ____________
            //      |            |
            //      |            |
            //      |            |
            //      |            |
            //      |____________|
            //     /
            //  OpenGL (0,0)
            //
            auto glBottom = RTHeight - Rect.bottom;

            auto width  = Rect.right - Rect.left;
            auto height = Rect.bottom - Rect.top;
            glScissor( Rect.left, glBottom, width, height );
            CHECK_GL_ERROR( "Failed to set scissor rect" );
        }
        else
        {
            for (Uint32 sr = 0; sr < NumRects; ++sr)
            {
                const auto& Rect = m_ScissorRects[sr];
                auto glBottom = RTHeight - Rect.bottom;
                auto width  = Rect.right - Rect.left;
                auto height = Rect.bottom - Rect.top;
                glScissorIndexed(sr, Rect.left, glBottom, width, height );
                CHECK_GL_ERROR("Failed to set scissor rect #", sr);
            }
        }
    }

    void DeviceContextGLImpl::CommitRenderTargets()
    {
        if (m_IsDefaultFramebufferBound)
        {
            auto* pSwapChainGL = m_pSwapChain.RawPtr<ISwapChainGL>();
            GLuint DefaultFBOHandle = pSwapChainGL->GetDefaultFBO();
            if (m_DefaultFBO != DefaultFBOHandle)
            {
                m_DefaultFBO = GLObjectWrappers::GLFrameBufferObj(true, GLObjectWrappers::GLFBOCreateReleaseHelper(DefaultFBOHandle));
            }
            m_ContextState.BindFBO(m_DefaultFBO);
        }
        else
        {
            VERIFY(m_NumBoundRenderTargets != 0 || m_pBoundDepthStencil, "At least one render target or a depth stencil is expected");

            Uint32 NumRenderTargets = m_NumBoundRenderTargets;
            VERIFY(NumRenderTargets < MaxRenderTargets, "Too many render targets (", NumRenderTargets, ") are being set");
            NumRenderTargets = std::min(NumRenderTargets, MaxRenderTargets);

            const auto& CtxCaps = m_ContextState.GetContextCaps();
            VERIFY(NumRenderTargets < static_cast<Uint32>(CtxCaps.m_iMaxDrawBuffers), "This device only supports ", CtxCaps.m_iMaxDrawBuffers, " draw buffers, but ", NumRenderTargets, " are being set");
            NumRenderTargets = std::min(NumRenderTargets, static_cast<Uint32>(CtxCaps.m_iMaxDrawBuffers));

            ITextureView* pBoundRTVs[MaxRenderTargets] = {};
            for (Uint32 rt = 0; rt < NumRenderTargets; ++rt)
                pBoundRTVs[rt] = m_pBoundRenderTargets[rt];

            auto CurrentNativeGLContext = m_ContextState.GetCurrentGLContext();
            auto& FBOCache = m_pDevice->GetFBOCache(CurrentNativeGLContext);
            const auto& FBO = FBOCache.GetFBO(NumRenderTargets, pBoundRTVs, m_pBoundDepthStencil, m_ContextState);
            // Even though the write mask only applies to writes to a framebuffer, the mask state is NOT 
            // Framebuffer state. So it is NOT part of a Framebuffer Object or the Default Framebuffer. 
            // Binding a new framebuffer will NOT affect the mask.
            m_ContextState.BindFBO(FBO);
        }
        // Set the viewport to match the render target size
        SetViewports(1, nullptr, 0, 0);
    }

    void DeviceContextGLImpl::SetRenderTargets(Uint32                         NumRenderTargets,
                                               ITextureView*                  ppRenderTargets[],
                                               ITextureView*                  pDepthStencil,
                                               RESOURCE_STATE_TRANSITION_MODE StateTransitionMode)
    {
        if (TDeviceContextBase::SetRenderTargets( NumRenderTargets, ppRenderTargets, pDepthStencil))
            CommitRenderTargets();
    }

    void DeviceContextGLImpl::BindProgramResources(Uint32& NewMemoryBarriers, IShaderResourceBinding* pResBinding)
    {
        if (!m_pPipelineState)
        {
            LOG_ERROR_MESSAGE("No pipeline state is bound");
            return;
        }

        if (pResBinding == nullptr)
            return;

        auto* pShaderResBindingGL = ValidatedCast<ShaderResourceBindingGLImpl>(pResBinding);
        const auto& ResourceCache = pShaderResBindingGL->GetResourceCache(m_pPipelineState);
#ifdef DEVELOPMENT
        m_pPipelineState->GetResourceLayout().dvpVerifyBindings(ResourceCache);
#endif

        VERIFY_EXPR(m_BoundWritableTextures.empty());
        VERIFY_EXPR(m_BoundWritableBuffers.empty());

        for (Uint32 ub = 0; ub < ResourceCache.GetUBCount(); ++ub)
        {
            const auto& UB = ResourceCache.GetConstUB(ub);
            if (!UB.pBuffer)
                continue;

            auto* pBufferGL = UB.pBuffer.RawPtr<BufferGLImpl>();
            pBufferGL->BufferMemoryBarrier(
                GL_UNIFORM_BARRIER_BIT,// Shader uniforms sourced from buffer objects after the barrier 
                                        // will reflect data written by shaders prior to the barrier
                m_ContextState);

            m_ContextState.BindUniformBuffer(ub, pBufferGL->m_GlBuffer);
            //glBindBufferRange(GL_UNIFORM_BUFFER, it->Index, pBufferGL->m_GlBuffer, 0, pBufferGL->GetDesc().uiSizeInBytes);
        }

        for (Uint32 s = 0; s < ResourceCache.GetSamplerCount(); ++s)
        {
            const auto& Sam = ResourceCache.GetConstSampler(s);
            if (!Sam.pView)
                continue;

            // We must check 'pTexture' first as 'pBuffer' is in union with 'pSampler'
            if (Sam.pTexture != nullptr)
            {
                auto* pTexViewGL = Sam.pView.RawPtr<TextureViewGLImpl>();
                auto* pTextureGL = ValidatedCast<TextureBaseGL>(Sam.pTexture);
                VERIFY_EXPR(pTextureGL == pTexViewGL->GetTexture());
                m_ContextState.BindTexture(s, pTexViewGL->GetBindTarget(), pTexViewGL->GetHandle());

                pTextureGL->TextureMemoryBarrier(
                    GL_TEXTURE_FETCH_BARRIER_BIT, // Texture fetches from shaders, including fetches from buffer object 
                                                  // memory via buffer textures, after the barrier will reflect data 
                                                  // written by shaders prior to the barrier
                    m_ContextState);

                if (Sam.pSampler)
                {
                    m_ContextState.BindSampler(s, Sam.pSampler->GetHandle());
                }
                else
                {
                    m_ContextState.BindSampler(s, GLObjectWrappers::GLSamplerObj(false));
                }
            }
            else if (Sam.pBuffer != nullptr)
            {
                auto* pBufViewGL = Sam.pView.RawPtr<BufferViewGLImpl>();
                auto* pBufferGL = ValidatedCast<BufferGLImpl>(Sam.pBuffer);
                VERIFY_EXPR(pBufferGL == pBufViewGL->GetBuffer());

                m_ContextState.BindTexture(s, GL_TEXTURE_BUFFER, pBufViewGL->GetTexBufferHandle());
                m_ContextState.BindSampler(s, GLObjectWrappers::GLSamplerObj(false)); // Use default texture sampling parameters

                pBufferGL->BufferMemoryBarrier(
                    GL_TEXTURE_FETCH_BARRIER_BIT, // Texture fetches from shaders, including fetches from buffer object 
                                                  // memory via buffer textures, after the barrier will reflect data 
                                                  // written by shaders prior to the barrier
                    m_ContextState);
            }
        }

#if GL_ARB_shader_image_load_store
        for (Uint32 img = 0; img < ResourceCache.GetImageCount(); ++img)
        {
            const auto& Img = ResourceCache.GetConstImage(img);
            if (!Img.pView)
                continue;

            // We must check 'pTexture' first as 'pBuffer' is in union with 'pSampler'
            if (Img.pTexture != nullptr)
            {
                auto* pTexViewGL = Img.pView.RawPtr<TextureViewGLImpl>();
                auto* pTextureGL = ValidatedCast<TextureBaseGL>(Img.pTexture);
                VERIFY_EXPR(pTextureGL == pTexViewGL->GetTexture());

                const auto& ViewDesc = pTexViewGL->GetDesc();
                VERIFY( ViewDesc.ViewType == TEXTURE_VIEW_UNORDERED_ACCESS, "Unexpected buffer view type" );

                if (ViewDesc.AccessFlags & UAV_ACCESS_FLAG_WRITE)
                {
                    pTextureGL->TextureMemoryBarrier(
                        GL_SHADER_IMAGE_ACCESS_BARRIER_BIT,// Memory accesses using shader image load, store, and atomic built-in 
                                                            // functions issued after the barrier will reflect data written by shaders 
                                                            // prior to the barrier. Additionally, image stores and atomics issued after 
                                                            // the barrier will not execute until all memory accesses (e.g., loads, 
                                                            // stores, texture fetches, vertex fetches) initiated prior to the barrier 
                                                            // complete.
                        m_ContextState);
                    // We cannot set pending memory barriers here, because
                    // if some texture is bound twice, the logic will fail
                    m_BoundWritableTextures.push_back( pTextureGL );
                }

#ifdef _DEBUG
                // Check that the texure being bound has immutable storage
                {
                    m_ContextState.BindTexture(-1, pTexViewGL->GetBindTarget(), pTexViewGL->GetHandle());
                    GLint IsImmutable = 0;
                    glGetTexParameteriv( pTexViewGL->GetBindTarget(), GL_TEXTURE_IMMUTABLE_FORMAT, &IsImmutable );
                    CHECK_GL_ERROR( "glGetTexParameteriv() failed" );
                    VERIFY( IsImmutable, "Only immutable textures can be bound to pipeline using glBindImageTexture()" );
                    m_ContextState.BindTexture( -1, pTexViewGL->GetBindTarget(), GLObjectWrappers::GLTextureObj::Null() );
                }
#endif
                auto GlTexFormat = TexFormatToGLInternalTexFormat( ViewDesc.Format );
                // Note that if a format qulifier is specified in the shader, the format
                // must match it

                GLboolean Layered = ViewDesc.NumArraySlices > 1 && ViewDesc.FirstArraySlice == 0;
                // If "layered" is TRUE, the entire Mip level is bound. Layer parameter is ignored in this
                // case. If "layered" is FALSE, only the single layer identified by "layer" will
                // be bound. When "layered" is FALSE, the single bound layer is treated as a 2D texture.
                GLint Layer = ViewDesc.FirstArraySlice;

                auto GLAccess = AccessFlags2GLAccess(ViewDesc.AccessFlags);
                // WARNING: Texture being bound to the image unit must be complete
                // That means that if an integer texture is being bound, its 
                // GL_TEXTURE_MIN_FILTER and GL_TEXTURE_MAG_FILTER must be NEAREST,
                // otherwise it will be incomplete
                m_ContextState.BindImage(img, pTexViewGL, ViewDesc.MostDetailedMip, Layered, Layer, GLAccess, GlTexFormat);
                // Do not use binding points from reflection as they may not be initialized
            }
            else if (Img.pBuffer != nullptr)
            {
                auto* pBuffViewGL = Img.pView.RawPtr<BufferViewGLImpl>();
                auto* pBufferGL   = ValidatedCast<BufferGLImpl>(Img.pBuffer);
                VERIFY_EXPR(pBufferGL == pBuffViewGL->GetBuffer());

                const auto& ViewDesc = pBuffViewGL->GetDesc();
                VERIFY( ViewDesc.ViewType == BUFFER_VIEW_UNORDERED_ACCESS, "Unexpected buffer view type" );

                pBufferGL->BufferMemoryBarrier(
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT,// Memory accesses using shader image load, store, and atomic built-in 
                                                        // functions issued after the barrier will reflect data written by shaders 
                                                        // prior to the barrier. Additionally, image stores and atomics issued after 
                                                        // the barrier will not execute until all memory accesses (e.g., loads, 
                                                        // stores, texture fetches, vertex fetches) initiated prior to the barrier 
                                                        // complete.
                    m_ContextState);

                m_BoundWritableBuffers.push_back(pBufferGL);

                auto GlFormat = TypeToGLTexFormat(ViewDesc.Format.ValueType, ViewDesc.Format.NumComponents, ViewDesc.Format.IsNormalized);
                m_ContextState.BindImage(img, pBuffViewGL, GL_READ_WRITE, GlFormat);
            }
        }
#endif


#if GL_ARB_shader_storage_buffer_object
        for (Uint32 ssbo = 0; ssbo < ResourceCache.GetSSBOCount(); ++ssbo)
        {
            const auto& SSBO = ResourceCache.GetConstSSBO(ssbo);
            if (!SSBO.pBufferView)
                return;
            
            auto* pBufferViewGL = SSBO.pBufferView.RawPtr<BufferViewGLImpl>();
            const auto& ViewDesc = pBufferViewGL->GetDesc();
            VERIFY( ViewDesc.ViewType == BUFFER_VIEW_UNORDERED_ACCESS || ViewDesc.ViewType == BUFFER_VIEW_SHADER_RESOURCE, "Unexpected buffer view type" );

            auto* pBufferGL = pBufferViewGL->GetBuffer<BufferGLImpl>();
            pBufferGL->BufferMemoryBarrier(
                GL_SHADER_STORAGE_BARRIER_BIT,// Accesses to shader storage blocks after the barrier 
                                              // will reflect writes prior to the barrier
                m_ContextState);

            m_ContextState.BindStorageBlock(ssbo, pBufferGL->m_GlBuffer, ViewDesc.ByteOffset, ViewDesc.ByteWidth);

            if (ViewDesc.ViewType == BUFFER_VIEW_UNORDERED_ACCESS)
                m_BoundWritableBuffers.push_back(pBufferGL);
        }
#endif


#if GL_ARB_shader_image_load_store
        // Go through the list of textures bound as AUVs and set the required memory barriers
        for (auto* pWritableTex : m_BoundWritableTextures)
        {
            Uint32 TextureMemBarriers =
                GL_TEXTURE_UPDATE_BARRIER_BIT // Writes to a texture via glTex(Sub)Image*, glCopyTex(Sub)Image*, 
                                              // glClearTex*Image, glCompressedTex(Sub)Image*, and reads via
                                              // glGetTexImage() after the barrier will reflect data written by 
                                              // shaders prior to the barrier

                | GL_TEXTURE_FETCH_BARRIER_BIT  // Texture fetches from shaders, including fetches from buffer object 
                                                // memory via buffer textures, after the barrier will reflect data 
                                                // written by shaders prior to the barrier

                | GL_PIXEL_BUFFER_BARRIER_BIT // Reads and writes of buffer objects via the GL_PIXEL_PACK_BUFFER and
                                              // GL_PIXEL_UNPACK_BUFFER bidnings after the barrier will reflect data 
                                              // written by shaders prior to the barrier

                | GL_FRAMEBUFFER_BARRIER_BIT // Reads and writes via framebuffer object attachments after the 
                                             // barrier will reflect data written by shaders prior to the barrier. 
                                             // Additionally, framebuffer writes issued after the barrier will wait 
                                             // on the completion of all shader writes issued prior to the barrier.

                | GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

            NewMemoryBarriers |= TextureMemBarriers;

            // Set new required barriers for the time when texture is used next time
            pWritableTex->SetPendingMemoryBarriers(TextureMemBarriers);
        }
        m_BoundWritableTextures.clear();

        for (auto* pWritableBuff : m_BoundWritableBuffers)
        {
            Uint32 BufferMemoryBarriers =
                GL_VERTEX_ATTRIB_ARRAY_BARRIER_BIT  |
                GL_ELEMENT_ARRAY_BARRIER_BIT        |
                GL_UNIFORM_BARRIER_BIT              |
                GL_COMMAND_BARRIER_BIT              | 
                GL_BUFFER_UPDATE_BARRIER_BIT        |
                GL_CLIENT_MAPPED_BUFFER_BARRIER_BIT |
                GL_SHADER_STORAGE_BARRIER_BIT       |
                GL_TEXTURE_FETCH_BARRIER_BIT        |
                GL_SHADER_IMAGE_ACCESS_BARRIER_BIT;

            NewMemoryBarriers |= BufferMemoryBarriers;
            // Set new required barriers for the time when buffer is used next time
            pWritableBuff->SetPendingMemoryBarriers( BufferMemoryBarriers );
        }
        m_BoundWritableBuffers.clear();
#endif
    }

    void DeviceContextGLImpl::PrepareForDraw(DRAW_FLAGS Flags, bool IsIndexed, GLenum& GlTopology)
    {
#ifdef DEVELOPMENT
        if ((Flags & DRAW_FLAG_VERIFY_RENDER_TARGETS) != 0)
            DvpVerifyRenderTargets();
#endif

        m_pPipelineState->CommitProgram(m_ContextState);

        auto CurrNativeGLContext = m_pDevice->m_GLContext.GetCurrentNativeGLContext();
        const auto& PipelineDesc = m_pPipelineState->GetDesc().GraphicsPipeline;
        if (!m_ContextState.IsValidVAOBound())
        {
            auto& VAOCache = m_pDevice->GetVAOCache(CurrNativeGLContext);
            IBuffer* pIndexBuffer = IsIndexed ? m_pIndexBuffer.RawPtr() : nullptr;
            if (PipelineDesc.InputLayout.NumElements > 0 || pIndexBuffer != nullptr)
            {
                const auto& VAO = VAOCache.GetVAO( m_pPipelineState, pIndexBuffer, m_VertexStreams, m_NumVertexStreams, m_ContextState );
                m_ContextState.BindVAO( VAO );
            }
            else
            {
                // Draw command will fail if no VAO is bound. If no vertex description is set
                // (which is the case if, for instance, the command only inputs VertexID),
                // use empty VAO
                const auto& VAO = VAOCache.GetEmptyVAO();
                m_ContextState.BindVAO( VAO );
            }
        }

        auto Topology = PipelineDesc.PrimitiveTopology;
        if (Topology >= PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST)
        {
#if GL_ARB_tessellation_shader
            GlTopology = GL_PATCHES;
            auto NumVertices = static_cast<Int32>(Topology - PRIMITIVE_TOPOLOGY_1_CONTROL_POINT_PATCHLIST + 1);
            m_ContextState.SetNumPatchVertices(NumVertices);
#else
            UNSUPPORTED("Tessellation is not supported");
#endif
        }
        else
        {
            GlTopology = PrimitiveTopologyToGLTopology( Topology );
        }
    }

    void DeviceContextGLImpl::PrepareForIndexedDraw(VALUE_TYPE IndexType, Uint32 FirstIndexLocation,  GLenum& GLIndexType, Uint32& FirstIndexByteOffset)
    {
        GLIndexType = TypeToGLType( IndexType );
        VERIFY( GLIndexType == GL_UNSIGNED_BYTE || GLIndexType == GL_UNSIGNED_SHORT || GLIndexType == GL_UNSIGNED_INT,
                "Unsupported index type" );
        VERIFY( m_pIndexBuffer, "Index Buffer is not bound to the pipeline" );
        FirstIndexByteOffset = static_cast<Uint32>(GetValueSize( IndexType )) * FirstIndexLocation + m_IndexDataStartOffset;
    }

    void DeviceContextGLImpl::PostDraw()
    {
        // IMPORTANT: new pending memory barriers in the context must be set
        // after all previous barriers have been executed.
        // m_CommitedResourcesTentativeBarriers contains memory barriers that will be required 
        // AFTER the actual draw/dispatch command is executed. 
        m_ContextState.SetPendingMemoryBarriers( m_CommitedResourcesTentativeBarriers );
        m_CommitedResourcesTentativeBarriers = 0;
    }

    void DeviceContextGLImpl::Draw(const DrawAttribs& Attribs)
    {
        if (!DvpVerifyDrawArguments(Attribs))
            return;

        GLenum GlTopology;
        PrepareForDraw(Attribs.Flags, false, GlTopology);

        if (Attribs.NumInstances > 1 || Attribs.FirstInstanceLocation != 0)
        {
            if (Attribs.FirstInstanceLocation != 0)
                glDrawArraysInstancedBaseInstance(GlTopology, Attribs.StartVertexLocation, Attribs.NumVertices, Attribs.NumInstances, Attribs.FirstInstanceLocation);
            else
                glDrawArraysInstanced(GlTopology, Attribs.StartVertexLocation, Attribs.NumVertices, Attribs.NumInstances);
        }
        else
        {
            glDrawArrays(GlTopology, Attribs.StartVertexLocation, Attribs.NumVertices);
        }
        DEV_CHECK_GL_ERROR( "OpenGL draw command failed" );

        PostDraw();
    }

    void DeviceContextGLImpl::DrawIndexed(const DrawIndexedAttribs& Attribs)
    {
        if (!DvpVerifyDrawIndexedArguments(Attribs))
            return;

        GLenum GlTopology;
        PrepareForDraw(Attribs.Flags, true, GlTopology);
        GLenum GLIndexType;
        Uint32 FirstIndexByteOffset;
        PrepareForIndexedDraw(Attribs.IndexType, Attribs.FirstIndexLocation, GLIndexType, FirstIndexByteOffset);

        // NOTE: Base Vertex and Base Instance versions are not supported even in OpenGL ES 3.1
        // This functionality can be emulated by adjusting stream offsets. This, however may cause
        // errors in case instance data is read from the same stream as vertex data. Thus handling
        // such cases is left to the application

        if (Attribs.NumInstances > 1  || Attribs.FirstInstanceLocation != 0)
        {
            if (Attribs.BaseVertex > 0)
            {
                if (Attribs.FirstInstanceLocation != 0)
                    glDrawElementsInstancedBaseVertexBaseInstance(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset) ), Attribs.NumInstances, Attribs.BaseVertex, Attribs.FirstInstanceLocation);
                else
                    glDrawElementsInstancedBaseVertex(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset) ), Attribs.NumInstances, Attribs.BaseVertex);
            }
            else
            {
                if (Attribs.FirstInstanceLocation != 0)
                    glDrawElementsInstancedBaseInstance(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset) ), Attribs.NumInstances, Attribs.FirstInstanceLocation);
                else
                    glDrawElementsInstanced(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset) ), Attribs.NumInstances);
            }
        }
        else
        {
            if (Attribs.BaseVertex > 0)
                glDrawElementsBaseVertex(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset) ), Attribs.BaseVertex);
            else
                glDrawElements(GlTopology, Attribs.NumIndices, GLIndexType, reinterpret_cast<GLvoid*>( static_cast<size_t>(FirstIndexByteOffset)));
        }
        DEV_CHECK_GL_ERROR( "OpenGL draw command failed" );

        PostDraw();
    }

    void  DeviceContextGLImpl::PrepareForIndirectDraw(IBuffer* pAttribsBuffer)
    {
#if GL_ARB_draw_indirect
        auto* pIndirectDrawAttribsGL = ValidatedCast<BufferGLImpl>(pAttribsBuffer);
        // The indirect rendering functions take their data from the buffer currently bound to the 
        // GL_DRAW_INDIRECT_BUFFER binding. Thus, any of indirect draw functions will fail if no buffer is 
        // bound to that binding.
        pIndirectDrawAttribsGL->BufferMemoryBarrier(
            GL_COMMAND_BARRIER_BIT,// Command data sourced from buffer objects by
                                    // Draw*Indirect and DispatchComputeIndirect commands after the barrier
                                    // will reflect data written by shaders prior to the barrier.The buffer 
                                    // objects affected by this bit are derived from the DRAW_INDIRECT_BUFFER 
                                    // and DISPATCH_INDIRECT_BUFFER bindings.
            m_ContextState);
        constexpr bool ResetVAO = false; // GL_DRAW_INDIRECT_BUFFER does not affect VAO
        m_ContextState.BindBuffer( GL_DRAW_INDIRECT_BUFFER, pIndirectDrawAttribsGL->m_GlBuffer, ResetVAO);
#endif
    }

    void DeviceContextGLImpl::DrawIndirect(const DrawIndirectAttribs& Attribs, IBuffer* pAttribsBuffer)
    {
        if (!DvpVerifyDrawIndirectArguments(Attribs, pAttribsBuffer))
            return;

#if GL_ARB_draw_indirect
        GLenum GlTopology;
        PrepareForDraw(Attribs.Flags, true, GlTopology);

        // http://www.opengl.org/wiki/Vertex_Rendering
        PrepareForIndirectDraw(pAttribsBuffer);

        //typedef  struct {
        //   GLuint  count;
        //   GLuint  instanceCount;
        //   GLuint  first;
        //   GLuint  baseInstance;
        //} DrawArraysIndirectCommand;
        glDrawArraysIndirect( GlTopology, reinterpret_cast<const void*>( static_cast<size_t>(Attribs.IndirectDrawArgsOffset) ) );
        // Note that on GLES 3.1, baseInstance is present but reserved and must be zero
        DEV_CHECK_GL_ERROR( "glDrawArraysIndirect() failed" );

        constexpr bool ResetVAO = false; // GL_DRAW_INDIRECT_BUFFER does not affect VAO
        m_ContextState.BindBuffer( GL_DRAW_INDIRECT_BUFFER, GLObjectWrappers::GLBufferObj::Null(), ResetVAO );

        PostDraw();
#else
        LOG_ERROR_MESSAGE("Indirect rendering is not supported");
#endif
    }

    void DeviceContextGLImpl::DrawIndexedIndirect(const DrawIndexedIndirectAttribs& Attribs, IBuffer* pAttribsBuffer)
    {
        if (!DvpVerifyDrawIndexedIndirectArguments(Attribs, pAttribsBuffer))
            return;

#if GL_ARB_draw_indirect
        GLenum GlTopology;
        PrepareForDraw(Attribs.Flags, true, GlTopology);
        GLenum GLIndexType;
        Uint32 FirstIndexByteOffset;
        PrepareForIndexedDraw(Attribs.IndexType, 0, GLIndexType, FirstIndexByteOffset);

        // http://www.opengl.org/wiki/Vertex_Rendering
        PrepareForIndirectDraw(pAttribsBuffer);

        //typedef  struct {
        //    GLuint  count;
        //    GLuint  instanceCount;
        //    GLuint  firstIndex;
        //    GLuint  baseVertex;
        //    GLuint  baseInstance;
        //} DrawElementsIndirectCommand;
        glDrawElementsIndirect( GlTopology, GLIndexType, reinterpret_cast<const void*>( static_cast<size_t>(Attribs.IndirectDrawArgsOffset) ) );
        // Note that on GLES 3.1, baseInstance is present but reserved and must be zero
        DEV_CHECK_GL_ERROR( "glDrawElementsIndirect() failed" );

        constexpr bool ResetVAO = false; // GL_DISPATCH_INDIRECT_BUFFER does not affect VAO
        m_ContextState.BindBuffer( GL_DRAW_INDIRECT_BUFFER, GLObjectWrappers::GLBufferObj::Null(), ResetVAO );

        PostDraw();
#else
        LOG_ERROR_MESSAGE("Indirect rendering is not supported");
#endif
    }


    void DeviceContextGLImpl::DispatchCompute(const DispatchComputeAttribs& Attribs)
    {
        if (!DvpVerifyDispatchArguments(Attribs))
            return;

#if GL_ARB_compute_shader
        m_pPipelineState->CommitProgram(m_ContextState);
        glDispatchCompute(Attribs.ThreadGroupCountX, Attribs.ThreadGroupCountY, Attribs.ThreadGroupCountZ);
        CHECK_GL_ERROR("glDispatchCompute() failed");

        PostDraw();
#else
        UNSUPPORTED("Compute shaders are not supported");
#endif
    }

    void DeviceContextGLImpl::DispatchComputeIndirect(const DispatchComputeIndirectAttribs& Attribs, IBuffer* pAttribsBuffer)
    {
        if (!DvpVerifyDispatchIndirectArguments(Attribs, pAttribsBuffer))
            return;

#if GL_ARB_compute_shader
        m_pPipelineState->CommitProgram(m_ContextState);

        auto* pBufferGL = ValidatedCast<BufferGLImpl>(pAttribsBuffer);
        pBufferGL->BufferMemoryBarrier(
            GL_COMMAND_BARRIER_BIT,// Command data sourced from buffer objects by
                                    // Draw*Indirect and DispatchComputeIndirect commands after the barrier
                                    // will reflect data written by shaders prior to the barrier.The buffer 
                                    // objects affected by this bit are derived from the DRAW_INDIRECT_BUFFER 
                                    // and DISPATCH_INDIRECT_BUFFER bindings.
            m_ContextState);

        constexpr bool ResetVAO = false; // GL_DISPATCH_INDIRECT_BUFFER does not affect VAO
        m_ContextState.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, pBufferGL->m_GlBuffer, ResetVAO);
        CHECK_GL_ERROR( "Failed to bind a buffer for dispatch indirect command" );

        glDispatchComputeIndirect(Attribs.DispatchArgsByteOffset);
        CHECK_GL_ERROR("glDispatchComputeIndirect() failed");

        m_ContextState.BindBuffer(GL_DISPATCH_INDIRECT_BUFFER, GLObjectWrappers::GLBufferObj::Null(), ResetVAO);
        
        PostDraw();
#else
        UNSUPPORTED("Compute shaders are not supported");
#endif
    }

    void DeviceContextGLImpl::ClearDepthStencil(ITextureView*                  pView,
                                                CLEAR_DEPTH_STENCIL_FLAGS      ClearFlags,
                                                float                          fDepth,
                                                Uint8                          Stencil,
                                                RESOURCE_STATE_TRANSITION_MODE StateTransitionMode)
    {
        // Unlike OpenGL, in D3D10+, the full extent of the resource view is always cleared. 
        // Viewport and scissor settings are not applied.
        if (pView != nullptr)
        {
            VERIFY( pView->GetDesc().ViewType == TEXTURE_VIEW_DEPTH_STENCIL, "Incorrect view type: depth stencil is expected" );
            CHECK_DYNAMIC_TYPE( TextureViewGLImpl, pView );
            if (pView != m_pBoundDepthStencil)
            {
                UNEXPECTED( "Depth stencil buffer being cleared is not bound to the pipeline" );
                LOG_ERROR_MESSAGE( "Depth stencil buffer must be bound to the pipeline to be cleared" );
            }
        }
        else
        {
            if (!m_IsDefaultFramebufferBound)
            {
                UNEXPECTED( "Default depth stencil buffer being cleared is not bound to the pipeline" );
                LOG_ERROR_MESSAGE( "Default depth stencil buffer must be bound to the pipeline to be cleared" );
            }
        }
        Uint32 glClearFlags = 0;
        if( ClearFlags & CLEAR_DEPTH_FLAG )   glClearFlags |= GL_DEPTH_BUFFER_BIT;
        if( ClearFlags & CLEAR_STENCIL_FLAG ) glClearFlags |= GL_STENCIL_BUFFER_BIT;
        glClearDepthf( fDepth );
        glClearStencil( Stencil );
        // If depth writes are disabled, glClear() will not clear depth buffer!
        bool DepthWritesEnabled = m_ContextState.GetDepthWritesEnabled();
        m_ContextState.EnableDepthWrites( True );
        bool ScissorTestEnabled = m_ContextState.GetScissorTestEnabled();
        m_ContextState.EnableScissorTest( False );
        // The pixel ownership test, the scissor test, dithering, and the buffer writemasks affect 
        // the operation of glClear. The scissor box bounds the cleared region. Alpha function, 
        // blend function, logical operation, stenciling, texture mapping, and depth-buffering 
        // are ignored by glClear.
        glClear(glClearFlags);
        CHECK_GL_ERROR( "glClear() failed" );
        m_ContextState.EnableDepthWrites( DepthWritesEnabled );
        m_ContextState.EnableScissorTest( ScissorTestEnabled );
    }

    void DeviceContextGLImpl::ClearRenderTarget(ITextureView* pView, const float* RGBA, RESOURCE_STATE_TRANSITION_MODE StateTransitionMode)
    {
        // Unlike OpenGL, in D3D10+, the full extent of the resource view is always cleared. 
        // Viewport and scissor settings are not applied.

        Int32 RTIndex = -1;
        if (pView != nullptr)
        {
            VERIFY( pView->GetDesc().ViewType == TEXTURE_VIEW_RENDER_TARGET, "Incorrect view type: render target is expected" );
            CHECK_DYNAMIC_TYPE( TextureViewGLImpl, pView );
            for (Uint32 rt = 0; rt < m_NumBoundRenderTargets; ++rt)
            {
                if (m_pBoundRenderTargets[rt] == pView)
                {
                    RTIndex = rt;
                    break;
                }
            }

            if (RTIndex == -1)
            {
                UNEXPECTED( "Render target being cleared is not bound to the pipeline" );
                LOG_ERROR_MESSAGE( "Render target must be bound to the pipeline to be cleared" );
            }
        }
        else
        {
            if (m_IsDefaultFramebufferBound)
                RTIndex = 0;
            else
            {
                UNEXPECTED( "Default render target must be bound to the pipeline to be cleared" );
                LOG_ERROR_MESSAGE( "Default render target must be bound to the pipeline to be cleared" );
            }
        }

        static const float Zero[4] = { 0, 0, 0, 0 };
        if( RGBA == nullptr )
            RGBA = Zero;
        
        // The pixel ownership test, the scissor test, dithering, and the buffer writemasks affect 
        // the operation of glClear. The scissor box bounds the cleared region. Alpha function, 
        // blend function, logical operation, stenciling, texture mapping, and depth-buffering 
        // are ignored by glClear.

        // Disable scissor test
        bool ScissorTestEnabled = m_ContextState.GetScissorTestEnabled();
        m_ContextState.EnableScissorTest( False );

        // Set write mask
        Uint32 WriteMask = 0;
        Bool bIndependentBlend = False;
        m_ContextState.GetColorWriteMask( RTIndex, WriteMask, bIndependentBlend );
        m_ContextState.SetColorWriteMask( RTIndex, COLOR_MASK_ALL, bIndependentBlend );
        
        glClearBufferfv( GL_COLOR, RTIndex, RGBA );
        CHECK_GL_ERROR( "glClearBufferfv() failed" );

        m_ContextState.SetColorWriteMask( RTIndex, WriteMask, bIndependentBlend );
        m_ContextState.EnableScissorTest( ScissorTestEnabled );
    }

    void DeviceContextGLImpl::Flush()
    {
        glFlush();
    }

    void DeviceContextGLImpl::FinishFrame()
    {
    }

    void DeviceContextGLImpl::FinishCommandList(class ICommandList** ppCommandList)
    {
        LOG_ERROR("Deferred contexts are not supported in OpenGL mode");
    }

    void DeviceContextGLImpl::ExecuteCommandList(class ICommandList* pCommandList)
    {
        LOG_ERROR("Deferred contexts are not supported in OpenGL mode");
    }

    void DeviceContextGLImpl::SignalFence(IFence* pFence, Uint64 Value)
    {
        VERIFY(!m_bIsDeferred, "Fence can only be signaled from immediate context");
        GLObjectWrappers::GLSyncObj GLFence( glFenceSync(
                GL_SYNC_GPU_COMMANDS_COMPLETE, // Condition must always be GL_SYNC_GPU_COMMANDS_COMPLETE
                0 // Flags, must be 0
            )
        );
        CHECK_GL_ERROR( "Failed to create gl fence" );
        auto* pFenceGLImpl = ValidatedCast<FenceGLImpl>(pFence);
        pFenceGLImpl->AddPendingFence(std::move(GLFence), Value);
    }

    void DeviceContextGLImpl::WaitForFence(IFence* pFence, Uint64 Value, bool FlushContext)
    {
        VERIFY(!m_bIsDeferred, "Fence can only be waited from immediate context");
        auto* pFenceGLImpl = ValidatedCast<FenceGLImpl>(pFence);
        pFenceGLImpl->Wait(Value, FlushContext);
    }

    void DeviceContextGLImpl::WaitForIdle()
    {
        VERIFY(!m_bIsDeferred, "Only immediate contexts can be idled");
        glFinish();
    }

    bool DeviceContextGLImpl::UpdateCurrentGLContext()
    {
        auto NativeGLContext = m_pDevice->m_GLContext.GetCurrentNativeGLContext();
        if (NativeGLContext == NULL)
            return false;

        m_ContextState.SetCurrentGLContext(NativeGLContext);
        return true;
    }

    void DeviceContextGLImpl::UpdateBuffer(IBuffer*                       pBuffer,
                                           Uint32                         Offset,
                                           Uint32                         Size,
                                           const PVoid                    pData,
                                           RESOURCE_STATE_TRANSITION_MODE StateTransitionMode)
    {
        TDeviceContextBase::UpdateBuffer(pBuffer, Offset, Size, pData, StateTransitionMode);

        auto* pBufferGL = ValidatedCast<BufferGLImpl>(pBuffer);
        pBufferGL->UpdateData(m_ContextState, Offset, Size, pData);
    }

    void DeviceContextGLImpl::CopyBuffer(IBuffer*                       pSrcBuffer,
                                         Uint32                         SrcOffset,
                                         RESOURCE_STATE_TRANSITION_MODE SrcBufferTransitionMode,
                                         IBuffer*                       pDstBuffer,
                                         Uint32                         DstOffset,
                                         Uint32                         Size,
                                         RESOURCE_STATE_TRANSITION_MODE DstBufferTransitionMode)
    {
        TDeviceContextBase::CopyBuffer(pSrcBuffer, SrcOffset, SrcBufferTransitionMode, pDstBuffer, DstOffset, Size, DstBufferTransitionMode);

        auto* pSrcBufferGL = ValidatedCast<BufferGLImpl>(pSrcBuffer);
        auto* pDstBufferGL = ValidatedCast<BufferGLImpl>(pDstBuffer);
        pDstBufferGL->CopyData(m_ContextState, *pSrcBufferGL, SrcOffset, DstOffset, Size);
    }

    void DeviceContextGLImpl::MapBuffer(IBuffer* pBuffer, MAP_TYPE MapType, MAP_FLAGS MapFlags, PVoid& pMappedData)
    {
        TDeviceContextBase::MapBuffer(pBuffer, MapType, MapFlags, pMappedData);
        auto* pBufferGL = ValidatedCast<BufferGLImpl>(pBuffer);
        pBufferGL->Map(m_ContextState, MapType, MapFlags, pMappedData);
    }

    void DeviceContextGLImpl::UnmapBuffer(IBuffer* pBuffer, MAP_TYPE MapType)
    {
        TDeviceContextBase::UnmapBuffer(pBuffer, MapType);
        auto* pBufferGL = ValidatedCast<BufferGLImpl>(pBuffer);
        pBufferGL->Unmap(m_ContextState);
    }

    void DeviceContextGLImpl::UpdateTexture(ITexture*                      pTexture,
                                            Uint32                         MipLevel,
                                            Uint32                         Slice,
                                            const Box&                     DstBox,
                                            const TextureSubResData&       SubresData,
                                            RESOURCE_STATE_TRANSITION_MODE SrcBufferStateTransitionMode,
                                            RESOURCE_STATE_TRANSITION_MODE TextureStateTransitionMode)
    {
        TDeviceContextBase::UpdateTexture( pTexture, MipLevel, Slice, DstBox, SubresData, SrcBufferStateTransitionMode, TextureStateTransitionMode );
        auto* pTexGL = ValidatedCast<TextureBaseGL>(pTexture);
        pTexGL->UpdateData(m_ContextState, MipLevel, Slice, DstBox, SubresData);
    }

    void DeviceContextGLImpl::CopyTexture(const CopyTextureAttribs& CopyAttribs)
    {
        TDeviceContextBase::CopyTexture( CopyAttribs );
        auto* pSrcTexGL = ValidatedCast<TextureBaseGL>(CopyAttribs.pSrcTexture);
        auto* pDstTexGL = ValidatedCast<TextureBaseGL>(CopyAttribs.pDstTexture);
        pDstTexGL->CopyData(this, pSrcTexGL, CopyAttribs.SrcMipLevel, CopyAttribs.SrcSlice, CopyAttribs.pSrcBox,
                            CopyAttribs.DstMipLevel, CopyAttribs.DstSlice, CopyAttribs.DstX, CopyAttribs.DstY, CopyAttribs.DstZ);
    }

    void DeviceContextGLImpl::MapTextureSubresource(ITexture*                 pTexture,
                                                    Uint32                    MipLevel,
                                                    Uint32                    ArraySlice,
                                                    MAP_TYPE                  MapType,
                                                    MAP_FLAGS                 MapFlags,
                                                    const Box*                pMapRegion,
                                                    MappedTextureSubresource& MappedData)
    {
        TDeviceContextBase::MapTextureSubresource(pTexture, MipLevel, ArraySlice, MapType, MapFlags, pMapRegion, MappedData);
        LOG_ERROR_MESSAGE("Texture mapping is not supported in OpenGL");
        MappedData = MappedTextureSubresource{};
    }


    void DeviceContextGLImpl::UnmapTextureSubresource(ITexture* pTexture, Uint32 MipLevel, Uint32 ArraySlice)
    {
        TDeviceContextBase::UnmapTextureSubresource( pTexture, MipLevel, ArraySlice);
        LOG_ERROR_MESSAGE("Texture mapping is not supported in OpenGL");
    }

    void DeviceContextGLImpl::GenerateMips( ITextureView* pTexView )
    {
        TDeviceContextBase::GenerateMips(pTexView);
        auto* pTexViewGL = ValidatedCast<TextureViewGLImpl>(pTexView);
        auto BindTarget = pTexViewGL->GetBindTarget();
        m_ContextState.BindTexture( -1, BindTarget, pTexViewGL->GetHandle() );
        glGenerateMipmap( BindTarget );
        CHECK_GL_ERROR( "Failed to generate mip maps" );
        m_ContextState.BindTexture( -1, BindTarget, GLObjectWrappers::GLTextureObj::Null() );
    }

    void DeviceContextGLImpl::TransitionResourceStates(Uint32 BarrierCount, StateTransitionDesc* pResourceBarriers)
    {

    }
}
