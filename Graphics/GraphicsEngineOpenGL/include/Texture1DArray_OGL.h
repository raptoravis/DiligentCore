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

#include "TextureBaseGL.h"

namespace Diligent
{

class Texture1DArray_OGL final : public TextureBaseGL
{
public:
    Texture1DArray_OGL(IReferenceCounters*         pRefCounters,
                       FixedBlockMemoryAllocator&  TexViewObjAllocator,
                       class RenderDeviceGLImpl*   pDeviceGL,
                       class GLContextState&       GLState,
                       const TextureDesc&          TexDesc,
                       const TextureData*          pInitData         = nullptr, 
                       bool                        bIsDeviceInternal = false);

    Texture1DArray_OGL(IReferenceCounters*         pRefCounters,
                       FixedBlockMemoryAllocator&  TexViewObjAllocator,
                       class RenderDeviceGLImpl*   pDeviceGL,
                       class GLContextState&       GLState,
                       const TextureDesc&          TexDesc,
                       GLuint                      GLTextureHandle,
				       bool                        bIsDeviceInternal = false);
    ~Texture1DArray_OGL();

    virtual void UpdateData( class GLContextState&    CtxState,
                             Uint32                   MipLevel,
                             Uint32                   Slice,
                             const Box&               DstBox,
                             const TextureSubResData& SubresData )override final;

    virtual void AttachToFramebuffer( const struct TextureViewDesc& ViewDesc,
                                      GLenum                        AttachmentPoint )override final;
};

}
