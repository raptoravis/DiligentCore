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

#if DILIGENT_USE_EASTL
#include "../../External/EASTL/include/EASTL/allocator.h"
#endif

#include "DefaultRawMemoryAllocator.h"

namespace Diligent
{
    DefaultRawMemoryAllocator::DefaultRawMemoryAllocator()
    {

    }

    void* DefaultRawMemoryAllocator::Allocate( size_t Size, const Char* dbgDescription, const char* dbgFileName, const  Int32 dbgLineNumber)
    {
#if DILIGENT_USE_EASTL
        return eastl::GetDefaultAllocator()->allocate(Size);
#else

#   ifdef _DEBUG
        return new Uint8[Size+16]+16;
#   else
        return new Uint8[Size];
#   endif

#endif
    }

    void* DefaultRawMemoryAllocator::Allocate( size_t      Size,
                                               size_t      Alignment,
                                               size_t      Offset,
                                               int         EASTLFlags,
                                               const Char* dbgDescription,
                                               const char* dbgFileName,
                                               const Int32 dbgLineNumber )
    {
#if DILIGENT_USE_EASTL
        if(Alignment != 0 || Offset != 0)
            return eastl::GetDefaultAllocator()->allocate(Size, Alignment, Offset, EASTLFlags);
        else
            return eastl::GetDefaultAllocator()->allocate(Size, EASTLFlags);
#else
        return Allocate(Size, dbgDescription, dbgFileName, dbgLineNumber);
#endif
    }

    void DefaultRawMemoryAllocator::Free(void *Ptr)
    {
#if DILIGENT_USE_EASTL
     eastl::GetDefaultAllocator()->deallocate(Ptr, 0);
#else
#   ifdef _DEBUG
        delete[] (reinterpret_cast<Uint8*>(Ptr)-16);
#   else
        delete[] reinterpret_cast<Uint8*>(Ptr);
#   endif
#endif
    }

    DefaultRawMemoryAllocator& DefaultRawMemoryAllocator::GetAllocator()
    {
        static DefaultRawMemoryAllocator Allocator;
        return Allocator;
    }
}

void* operator new[](size_t size, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    return new char[size];//eastl::GetDefaultAllocator()->allocate(size);
}

void* operator new[](size_t size, size_t alignment, size_t alignmentOffset, const char* pName, int flags, unsigned debugFlags, const char* file, int line)
{
    return new char[size];//seastl::GetDefaultAllocator()->allocate(size, alignment, alignmentOffset);
}
