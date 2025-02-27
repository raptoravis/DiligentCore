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

/// \file
/// Defines graphics engine utilities

#include "../../GraphicsEngine/interface/GraphicsTypes.h"
#include "../../GraphicsEngine/interface/Shader.h"
#include "../../GraphicsEngine/interface/Texture.h"
#include "../../GraphicsEngine/interface/Buffer.h"
#include "../../GraphicsEngine/interface/RenderDevice.h"
#include "../../../Platforms/Basic/interface/DebugUtilities.h"

namespace Diligent
{

/// Template structure to convert VALUE_TYPE enumeration into C-type
template<VALUE_TYPE ValType>
struct VALUE_TYPE2CType
{};
    
/// VALUE_TYPE2CType<> template specialization for 8-bit integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_INT8>::CType MyInt8Var;
template<>struct VALUE_TYPE2CType<VT_INT8>  { typedef Int8 CType; };

/// VALUE_TYPE2CType<> template specialization for 16-bit integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_INT16>::CType MyInt16Var;
template<>struct VALUE_TYPE2CType<VT_INT16> { typedef Int16 CType; };

/// VALUE_TYPE2CType<> template specialization for 32-bit integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_INT32>::CType MyInt32Var;
template<>struct VALUE_TYPE2CType<VT_INT32> { typedef Int32 CType; };
    
/// VALUE_TYPE2CType<> template specialization for 8-bit unsigned-integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_UINT8>::CType MyUint8Var;
template<>struct VALUE_TYPE2CType<VT_UINT8> { typedef Uint8 CType; };

/// VALUE_TYPE2CType<> template specialization for 16-bit unsigned-integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_UINT16>::CType MyUint16Var;
template<>struct VALUE_TYPE2CType<VT_UINT16>{ typedef Uint16 CType; };

/// VALUE_TYPE2CType<> template specialization for 32-bit unsigned-integer value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_UINT32>::CType MyUint32Var;
template<>struct VALUE_TYPE2CType<VT_UINT32>{ typedef Uint32 CType; };

/// VALUE_TYPE2CType<> template specialization for half-precision 16-bit floating-point value type.
    
/// Usage example:
///
///     VALUE_TYPE2CType<VT_FLOAT16>::CType MyFloat16Var;
///
/// \note 16-bit floating-point values have no corresponding C++ type and are translated to Uint16
template<>struct VALUE_TYPE2CType<VT_FLOAT16>{ typedef Uint16 CType; };

/// VALUE_TYPE2CType<> template specialization for full-precision 32-bit floating-point value type.

/// Usage example:
///
///     VALUE_TYPE2CType<VT_FLOAT32>::CType MyFloat32Var;
template<>struct VALUE_TYPE2CType<VT_FLOAT32>{ typedef Float32 CType; };


static const Uint32 ValueTypeToSizeMap[] = 
{
    0,
    sizeof(VALUE_TYPE2CType<VT_INT8>    :: CType),
    sizeof(VALUE_TYPE2CType<VT_INT16>   :: CType),
    sizeof(VALUE_TYPE2CType<VT_INT32>   :: CType),
    sizeof(VALUE_TYPE2CType<VT_UINT8>   :: CType),
    sizeof(VALUE_TYPE2CType<VT_UINT16>  :: CType),
    sizeof(VALUE_TYPE2CType<VT_UINT32>  :: CType),
    sizeof(VALUE_TYPE2CType<VT_FLOAT16> :: CType),
    sizeof(VALUE_TYPE2CType<VT_FLOAT32> :: CType)
};
static_assert(VT_NUM_TYPES == VT_FLOAT32 + 1, "Not all value type sizes initialized.");

/// Returns the size of the specified value type
inline Uint32 GetValueSize(VALUE_TYPE Val)
{
    VERIFY_EXPR(Val < _countof(ValueTypeToSizeMap));
    return ValueTypeToSizeMap[Val];
}

/// Returns the string representing the specified value type
const Char* GetValueTypeString( VALUE_TYPE Val );

/// Returns invariant texture format attributes, see TextureFormatAttribs for details.

/// \param [in] Format - Texture format which attributes are requested for.
/// \return Constant reference to the TextureFormatAttribs structure containing
///         format attributes.
const TextureFormatAttribs& GetTextureFormatAttribs(TEXTURE_FORMAT Format);

/// Returns the default format for a specified texture view type

/// The default view is defined as follows:
/// * For a fully qualified texture format, the SRV/RTV/UAV view format is the same as texture format;
///   DSV format, if avaialble, is adjusted accrodingly (R32_FLOAT -> D32_FLOAT)
/// * For 32-bit typeless formats, default view is XXXX32_FLOAT (where XXXX are the actual format components)\n
/// * For 16-bit typeless formats, default view is XXXX16_FLOAT (where XXXX are the actual format components)\n
/// ** R16_TYPELESS is special. If BIND_DEPTH_STENCIL flag is set, it is translated to R16_UNORM/D16_UNORM;
///    otherwise it is translated to R16_FLOAT.
/// * For 8-bit typeless formats, default view is XXXX8_UNORM (where XXXX are the actual format components)\n
/// * sRGB is always chosen if it is available (RGBA8_UNORM_SRGB, TEX_FORMAT_BC1_UNORM_SRGB, etc.)
/// * For combined depth-stencil formats, SRV format references depth component (R24_UNORM_X8_TYPELESS for D24S8 formats, and
///   R32_FLOAT_X8X24_TYPELESS for D32S8X24 formats)
/// * For compressed formats, only SRV format is defined
///
/// \param [in] Format - texture format, for which the view format is requested
/// \param [in] ViewType - texture view type
/// \param [in] BindFlags - texture bind flags
/// \return  texture view type format
TEXTURE_FORMAT GetDefaultTextureViewFormat(TEXTURE_FORMAT TextureFormat, TEXTURE_VIEW_TYPE ViewType, Uint32 BindFlags);

/// Returns the default format for a specified texture view type

/// \param [in] TexDesc - texture description
/// \param [in] ViewType - texture view type
/// \return  texture view type format
inline TEXTURE_FORMAT GetDefaultTextureViewFormat(const TextureDesc &TexDesc, TEXTURE_VIEW_TYPE ViewType)
{
    return GetDefaultTextureViewFormat(TexDesc.Format, ViewType, TexDesc.BindFlags);
}

/// Returns the literal name of a texture view type. For instance,
/// for a shader resource view, "TEXTURE_VIEW_SHADER_RESOURCE" will be returned.

/// \param [in] ViewType - Texture view type.
/// \return Literal name of the texture view type.
const Char* GetTexViewTypeLiteralName(TEXTURE_VIEW_TYPE ViewType);

/// Returns the literal name of a buffer view type. For instance,
/// for an unordered access view, "BUFFER_VIEW_UNORDERED_ACCESS" will be returned.

/// \param [in] ViewType - Buffer view type.
/// \return Literal name of the buffer view type.
const Char* GetBufferViewTypeLiteralName(BUFFER_VIEW_TYPE ViewType);

/// Returns the literal name of a shader type. For instance,
/// for a pixel shader, "SHADER_TYPE_PIXEL" will be returned.

/// \param [in] ShaderType - Shader type.
/// \return Literal name of the shader type.
const Char* GetShaderTypeLiteralName(SHADER_TYPE ShaderType);

/// \param [in] ShaderStages - Shader stages.
/// \return The string representing the shader stages. For example, 
///         if ShaderStages == SHADER_TYPE_VERTEX | SHADER_TYPE_PIXEL,
///         the following string will be returned:
///         "SHADER_TYPE_VERTEX, SHADER_TYPE_PIXEL"
String GetShaderStagesString(SHADER_TYPE ShaderStages);

/// Returns the literal name of a shader variable type. For instance,
/// for SHADER_RESOURCE_VARIABLE_TYPE_STATIC, if bGetFullName == true, "SHADER_RESOURCE_VARIABLE_TYPE_STATIC" will be returned;
/// if bGetFullName == false, "static" will be returned

/// \param [in] VarType - Variable type.
/// \param [in] bGetFullName - Whether to return string representation of the enum value
/// \return Literal name of the shader variable type.
const Char* GetShaderVariableTypeLiteralName(SHADER_RESOURCE_VARIABLE_TYPE VarType, bool bGetFullName = false);

/// Returns the literal name of a shader resource type. For instance,
/// for SHADER_RESOURCE_TYPE_CONSTANT_BUFFER, if bGetFullName == true, "SHADER_RESOURCE_TYPE_CONSTANT_BUFFER" will be returned;
/// if bGetFullName == false, "constant buffer" will be returned

/// \param [in] ResourceType - Resource type.
/// \param [in] bGetFullName - Whether to return string representation of the enum value
/// \return Literal name of the shader resource type.
const Char* GetShaderResourceTypeLiteralName(SHADER_RESOURCE_TYPE ResourceType, bool bGetFullName = false);

/// Overloaded function that returns the literal name of a texture view type.
/// see GetTexViewTypeLiteralName().
inline const Char* GetViewTypeLiteralName(TEXTURE_VIEW_TYPE TexViewType)
{
    return GetTexViewTypeLiteralName( TexViewType );
}

/// Overloaded function that returns the literal name of a buffer view type.
/// see GetBufferViewTypeLiteralName().
inline const Char* GetViewTypeLiteralName(BUFFER_VIEW_TYPE BuffViewType)
{
    return GetBufferViewTypeLiteralName( BuffViewType );
}

/// Returns the string containing the map type
const Char* GetMapTypeString(MAP_TYPE MapType);

/// Returns the string containing the usage
const Char* GetUsageString(USAGE Usage);

/// Returns the string containing the texture type
const Char* GetResourceDimString( RESOURCE_DIMENSION TexType );

/// Returns the string containing single bind flag
const Char* GetBindFlagString( Uint32 BindFlag );

/// Returns the string containing the bind flags
String GetBindFlagsString( Uint32 BindFlags );

/// Returns the string containing the CPU access flags
String GetCPUAccessFlagsString( Uint32 CpuAccessFlags );

/// Returns the string containing the texture description
String GetTextureDescString(const TextureDesc &Desc);

/// Returns the string containing the buffer format description
String GetBufferFormatString(const BufferFormat& Fmt);

/// Returns the string containing the buffer mode description
const Char* GetBufferModeString( BUFFER_MODE Mode );

/// Returns the string containing the buffer description
String GetBufferDescString(const BufferDesc &Desc);

/// Returns the string containing the buffer mode description
const Char* GetResourceStateFlagString( RESOURCE_STATE State );
String GetResourceStateString( RESOURCE_STATE State );

/// Helper template function that converts object description into a string
template<typename TObjectDescType>
String GetObjectDescString( const TObjectDescType& )
{
    return "";
}

/// Template specialization for texture description
template<>
inline String GetObjectDescString( const TextureDesc& TexDesc )
{
    String Str( "Tex desc: " );
    Str += GetTextureDescString( TexDesc );
    return Str;
}

/// Template specialization for buffer description
template<>
inline String GetObjectDescString( const BufferDesc& BuffDesc )
{
    String Str( "Buff desc: " );
    Str += GetBufferDescString( BuffDesc );
    return Str;
}

Uint32 ComputeMipLevelsCount( Uint32 Width );
Uint32 ComputeMipLevelsCount( Uint32 Width, Uint32 Height );
Uint32 ComputeMipLevelsCount( Uint32 Width, Uint32 Height, Uint32 Depth );

inline bool IsComparisonFilter(FILTER_TYPE FilterType)
{
    return FilterType == FILTER_TYPE_COMPARISON_POINT ||
           FilterType == FILTER_TYPE_COMPARISON_LINEAR ||
           FilterType == FILTER_TYPE_COMPARISON_ANISOTROPIC;
}

inline bool IsAnisotropicFilter(FILTER_TYPE FilterType)
{
    return FilterType == FILTER_TYPE_ANISOTROPIC ||
           FilterType == FILTER_TYPE_COMPARISON_ANISOTROPIC ||
           FilterType == FILTER_TYPE_MINIMUM_ANISOTROPIC || 
           FilterType == FILTER_TYPE_MAXIMUM_ANISOTROPIC;
}

bool VerifyResourceStates(RESOURCE_STATE State, bool IsTexture);

/// Describes the mip level properties
struct MipLevelProperties
{
    Uint32 LogicalWidth   = 0;
    Uint32 LogicalHeight  = 0;
    Uint32 StorageWidth   = 0;
    Uint32 StorageHeight  = 0;
    Uint32 Depth          = 1;
    Uint32 RowSize        = 0;
    Uint32 DepthSliceSize = 0;
    Uint32 MipSize        = 0;
};

MipLevelProperties GetMipLevelProperties(const TextureDesc& TexDesc, Uint32 MipLevel);

}
