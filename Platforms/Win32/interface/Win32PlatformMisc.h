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

#include "../../Basic/interface/BasicPlatformMisc.h"
#include "../../../Platforms/Basic/interface/DebugUtilities.h"

#include <intrin.h>

struct WindowsMisc : public BasicPlatformMisc
{
    inline static Diligent::Uint32 GetMSB(Diligent::Uint32 Val)
    {
        if( Val == 0 )return 32;

        unsigned long MSB = 32;
        _BitScanReverse(&MSB, Val);
        VERIFY_EXPR(MSB == BasicPlatformMisc::GetMSB(Val));

        return MSB;
    }

    inline static Diligent::Uint32 GetMSB(Diligent::Uint64 Val)
    {
        if( Val == 0 )return 64;

        unsigned long MSB = 64;
#if _WIN64
        _BitScanReverse64(&MSB, Val);
#else
        Diligent::Uint32 high = static_cast<Diligent::Uint32>((Val>>32) & 0xFFFFFFFF);
        if (high!=0)
        {
            MSB = 32 + GetMSB(high);
        }
        else
        {
            Diligent::Uint32 low = static_cast<Diligent::Uint32>(Val & 0xFFFFFFFF);
            VERIFY_EXPR(low != 0);
            MSB = GetMSB(low);
        }
#endif
        VERIFY_EXPR(MSB == BasicPlatformMisc::GetMSB(Val));

        return MSB;
    }

    inline static Diligent::Uint32 GetLSB(Diligent::Uint32 Val)
    {
        if( Val == 0 )return 32;

        unsigned long LSB = 32;
        _BitScanForward(&LSB, Val);
        VERIFY_EXPR(LSB == BasicPlatformMisc::GetLSB(Val));

        return LSB;
    }

    inline static Diligent::Uint32 GetLSB(Diligent::Uint64 Val)
    {
        if( Val == 0 )return 64;

        unsigned long LSB = 64;
#if _WIN64
        _BitScanForward64(&LSB, Val);
#else
        Diligent::Uint32 low = static_cast<Diligent::Uint32>(Val & 0xFFFFFFFF);
        if (low != 0)
        {
            LSB = GetLSB(low);
        }
        else
        {
            Diligent::Uint32 high = static_cast<Diligent::Uint32>((Val>>32) & 0xFFFFFFFF);
            VERIFY_EXPR(high != 0);
            LSB = 32 + GetLSB(high);
        }
#endif

        VERIFY_EXPR(LSB == BasicPlatformMisc::GetLSB(Val));
        return LSB;
    }

    inline static Diligent::Uint32 CountOneBits(Diligent::Uint32 Val)
    {
        auto Bits = __popcnt(Val);
        VERIFY_EXPR(Bits == BasicPlatformMisc::CountOneBits(Val));
        return Bits;
    }

    inline static Diligent::Uint32 CountOneBits(Diligent::Uint64 Val)
    {
#if _WIN64
        auto Bits = __popcnt64(Val);
#else
        auto Bits = CountOneBits( static_cast<Diligent::Uint32>((Val >>  0) & 0xFFFFFFFF) ) +
                    CountOneBits( static_cast<Diligent::Uint32>((Val >> 32) & 0xFFFFFFFF) );
#endif
        VERIFY_EXPR(Bits == BasicPlatformMisc::CountOneBits(Val));
        return static_cast<Diligent::Uint32>(Bits);
    }
};
