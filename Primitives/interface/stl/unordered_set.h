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

#pragma once

#include "functional.h"

#if DILIGENT_USE_EASTL

#include "../../External/EASTL/include/EASTL/hash_set.h"

namespace stl
{

template <typename  Key,
          typename  Hash           = eastl::hash<Key>,
          typename  Predicate      = eastl::equal_to<Key>, 
	      typename  Allocator      = EASTLAllocatorType,
          bool      bCacheHashCode = false>
using unordered_set = eastl::hash_set<Key, Hash, Predicate, Allocator, bCacheHashCode>;

template <typename  Key,
          typename  Hash           = eastl::hash<Key>,
          typename  Predicate      = eastl::equal_to<Key>, 
	      typename  Allocator      = EASTLAllocatorType,
          bool      bCacheHashCode = false>
using unordered_multiset = eastl::hash_multiset<Key, Hash, Predicate, Allocator, bCacheHashCode>;
}

#else

#include <unordered_set>

namespace stl
{

template <typename  Key,
          typename  Hash               = std::hash<Key>,
          typename  Predicate          = std::equal_to<Key>, 
	      typename  Allocator          = std::allocator<Key>,
          bool      /*bCacheHashCode*/ = false>
using unordered_set = std::unordered_set<Key, Hash, Predicate, Allocator>;

template <typename  Key,
          typename  Hash               = std::hash<Key>,
          typename  Predicate          = std::equal_to<Key>, 
	      typename  Allocator          = std::allocator<Key>,
          bool      /*bCacheHashCode*/ = false>
using unordered_multiset = std::unordered_multiset<Key, Hash, Predicate, Allocator>;

}

#endif
