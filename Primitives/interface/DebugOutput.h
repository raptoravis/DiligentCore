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

#include "BasicTypes.h"

namespace Diligent
{

/// Describes debug message severity
enum class DebugMessageSeverity : Int32
{
    /// Information message
    Info = 0,

    /// Warning message
    Warning,

    /// Error, with potential recovery
    Error,

    /// Fatal error - recovery is not possible
    FatalError
};


/// Type of the debug messag callback function

/// \param [in] Severity - Message severity
/// \param [in] Message - Debug message
/// \param [in] Function - Name of the function or nullptr
/// \param [in] Function - File name or nullptr
/// \param [in] Line - Line number
using DebugMessageCallbackType = void(*)(DebugMessageSeverity Severity, const Char* Message, const char* Function, const char* File, int Line);
extern DebugMessageCallbackType DebugMessageCallback;


/// Sets the debug message callback function

/// \note This function needs to be called for every executable module that
///       wants to use the callback.
void SetDebugMessageCallback(DebugMessageCallbackType DbgMessageCallback);

}
