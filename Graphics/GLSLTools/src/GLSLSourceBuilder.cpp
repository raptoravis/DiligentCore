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

#include <cstring>
#include <sstream>

#include "GLSLSourceBuilder.h"
#include "DebugUtilities.h"
#include "HLSL2GLSLConverterImpl.h"
#include "RefCntAutoPtr.h"
#include "DataBlobImpl.h"

namespace Diligent
{

String BuildGLSLSourceString(const ShaderCreateInfo& CreationAttribs,
                             const DeviceCaps&            deviceCaps,
                             TargetGLSLCompiler           TargetCompiler,
                             const char*                  ExtraDefinitions)
{
    String GLSLSource;

    auto ShaderType = CreationAttribs.Desc.ShaderType;

#if PLATFORM_WIN32 || PLATFORM_LINUX
    GLSLSource.append(
        "#version 430 core\n"
        "#define DESKTOP_GL 1\n"
    );
#   if PLATFORM_WIN32
    GLSLSource.append("#define PLATFORM_WIN32 1\n");
#   elif PLATFORM_LINUX
    GLSLSource.append("#define PLATFORM_LINUX 1\n");
#   else
#       error Unexpected platform
#   endif
#elif PLATFORM_MACOS
    if (TargetCompiler == TargetGLSLCompiler::glslang)
        GLSLSource.append("#version 430 core\n");
    else if (TargetCompiler == TargetGLSLCompiler::driver)
        GLSLSource.append("#version 410 core\n");
    else
        UNEXPECTED("Unexpected target GLSL compiler");

    GLSLSource.append(
        "#define DESKTOP_GL 1\n"
        "#define PLATFORM_MACOS 1\n"
    );

#elif PLATFORM_ANDROID || PLATFORM_IOS
    bool IsES30        = false;
    bool IsES31OrAbove = false;
    bool IsES32OrAbove = false;
    if (deviceCaps.DevType == DeviceType::Vulkan)
    {
        IsES30        = false;
        IsES31OrAbove = true;
        IsES32OrAbove = false;
        GLSLSource.append("#version 310 es\n");
    }
    else if (deviceCaps.DevType == DeviceType::OpenGLES)
    {
        IsES30        =                                 deviceCaps.MajorVersion == 3 && deviceCaps.MinorVersion == 0;
        IsES31OrAbove = deviceCaps.MajorVersion > 3 || (deviceCaps.MajorVersion == 3 && deviceCaps.MinorVersion >= 1);
        IsES32OrAbove = deviceCaps.MajorVersion > 3 || (deviceCaps.MajorVersion == 3 && deviceCaps.MinorVersion >= 2);
        std::stringstream versionss;
        versionss << "#version " << deviceCaps.MajorVersion << deviceCaps.MinorVersion << "0 es\n";
        GLSLSource.append(versionss.str());
    }
    else
    {
        UNEXPECTED("Unexpected device type");
    }

    if (deviceCaps.bSeparableProgramSupported && !IsES31OrAbove)
        GLSLSource.append("#extension GL_EXT_separate_shader_objects : enable\n");

    if (deviceCaps.TexCaps.bCubemapArraysSupported && !IsES32OrAbove)
        GLSLSource.append("#extension GL_EXT_texture_cube_map_array : enable\n");

    if (ShaderType == SHADER_TYPE_GEOMETRY && !IsES32OrAbove)
        GLSLSource.append("#extension GL_EXT_geometry_shader : enable\n");

    if ((ShaderType == SHADER_TYPE_HULL || ShaderType == SHADER_TYPE_DOMAIN) && !IsES32OrAbove)
        GLSLSource.append("#extension GL_EXT_tessellation_shader : enable\n");

    GLSLSource.append(
        "#ifndef GL_ES\n"
        "#  define GL_ES 1\n"
        "#endif\n"
    );

#if PLATFORM_ANDROID
    GLSLSource.append("#define PLATFORM_ANDROID 1\n");
#elif PLATFORM_IOS
    GLSLSource.append("#define PLATFORM_IOS 1\n");
#else
#   error "Unexpected platform"
#endif

    GLSLSource.append(
        "precision highp float;\n"
        "precision highp int;\n"
        //"precision highp uint;\n"  // This line causes shader compilation error on NVidia!

        "precision highp sampler2D;\n"
        "precision highp sampler3D;\n"
        "precision highp samplerCube;\n"
        "precision highp samplerCubeShadow;\n"

        "precision highp sampler2DShadow;\n"
        "precision highp sampler2DArray;\n"
        "precision highp sampler2DArrayShadow;\n"

        "precision highp isampler2D;\n"
        "precision highp isampler3D;\n"
        "precision highp isamplerCube;\n"
        "precision highp isampler2DArray;\n"

        "precision highp usampler2D;\n"
        "precision highp usampler3D;\n"
        "precision highp usamplerCube;\n"
        "precision highp usampler2DArray;\n"
    );

    if (IsES32OrAbove)
    {
        GLSLSource.append(
            "precision highp samplerBuffer;\n"
            "precision highp isamplerBuffer;\n"
            "precision highp usamplerBuffer;\n"
        );
    }

    if (deviceCaps.TexCaps.bCubemapArraysSupported)
    {
        GLSLSource.append(
            "precision highp samplerCubeArray;\n"
            "precision highp samplerCubeArrayShadow;\n"
            "precision highp isamplerCubeArray;\n"
            "precision highp usamplerCubeArray;\n"
        );
    }

    if (deviceCaps.TexCaps.bTexture2DMSSupported)
    {
        GLSLSource.append(
            "precision highp sampler2DMS;\n"
            "precision highp isampler2DMS;\n"
            "precision highp usampler2DMS;\n"
        );
    }

    if (deviceCaps.bComputeShadersSupported)
    {
        GLSLSource.append(
            "precision highp image2D;\n"
            "precision highp image3D;\n"
            "precision highp imageCube;\n"
            "precision highp image2DArray;\n"

            "precision highp iimage2D;\n"
            "precision highp iimage3D;\n"
            "precision highp iimageCube;\n"
            "precision highp iimage2DArray;\n"

            "precision highp uimage2D;\n"
            "precision highp uimage3D;\n"
            "precision highp uimageCube;\n"
            "precision highp uimage2DArray;\n"
        );
        if (IsES32OrAbove)
        {
            GLSLSource.append(
                "precision highp imageBuffer;\n"
                "precision highp iimageBuffer;\n"
                "precision highp uimageBuffer;\n"
            );
        }
    }

    if (IsES30 && deviceCaps.bSeparableProgramSupported && ShaderType == SHADER_TYPE_VERTEX)
    {
        // From https://www.khronos.org/registry/OpenGL/extensions/EXT/EXT_separate_shader_objects.gles.txt:
        //
        // When using GLSL ES 3.00 shaders in separable programs, gl_Position and 
        // gl_PointSize built-in outputs must be redeclared according to Section 7.5 
        // of the OpenGL Shading Language Specification...
        //   
        // add to GLSL ES 3.00 new section 7.5, Built-In Redeclaration and 
        // Separable Programs:
        // 
        // "The following vertex shader outputs may be redeclared at global scope to
        // specify a built-in output interface, with or without special qualifiers:
        // 
        //     gl_Position
        //     gl_PointSize
        //
        //   When compiling shaders using either of the above variables, both such
        //   variables must be redeclared prior to use.  ((Note:  This restriction
        //   applies only to shaders using version 300 that enable the
        //   EXT_separate_shader_objects extension; shaders not enabling the
        //   extension do not have this requirement.))  A separable program object
        //   will fail to link if any attached shader uses one of the above variables
        //   without redeclaration."
        GLSLSource.append("out vec4 gl_Position;\n");
    }

#elif
#   error "Undefined platform"
#endif

    // It would be much more convenient to use row_major matrices.
    // But unfortunatelly on NVIDIA, the following directive 
    // layout(std140, row_major) uniform;
    // does not have any effect on matrices that are part of structures
    // So we have to use column-major matrices which are default in both
    // DX and GLSL.
    GLSLSource.append(
        "layout(std140) uniform;\n"
    );

    if (ShaderType == SHADER_TYPE_VERTEX && TargetCompiler == TargetGLSLCompiler::glslang)
    {
        // https://github.com/KhronosGroup/GLSL/blob/master/extensions/khr/GL_KHR_vulkan_glsl.txt
        GLSLSource.append("#define gl_VertexID gl_VertexIndex\n"
                          "#define gl_InstanceID gl_InstanceIndex\n");
    }

    const Char* ShaderTypeDefine = nullptr;
    switch (ShaderType)
    {
        case SHADER_TYPE_VERTEX:    ShaderTypeDefine = "#define VERTEX_SHADER\n";         break;
        case SHADER_TYPE_PIXEL:     ShaderTypeDefine = "#define FRAGMENT_SHADER\n";       break;
        case SHADER_TYPE_GEOMETRY:  ShaderTypeDefine = "#define GEOMETRY_SHADER\n";       break;
        case SHADER_TYPE_HULL:      ShaderTypeDefine = "#define TESS_CONTROL_SHADER\n";   break;
        case SHADER_TYPE_DOMAIN:    ShaderTypeDefine = "#define TESS_EVALUATION_SHADER\n"; break;
        case SHADER_TYPE_COMPUTE:   ShaderTypeDefine = "#define COMPUTE_SHADER\n";        break;
        default: UNEXPECTED("Shader type is not specified");
    }
    GLSLSource += ShaderTypeDefine;

    if(ExtraDefinitions != nullptr)
    {
        GLSLSource.append(ExtraDefinitions);
    }

    if (CreationAttribs.Macros != nullptr)
    {
        auto *pMacro = CreationAttribs.Macros;
        while (pMacro->Name != nullptr && pMacro->Definition != nullptr)
        {
            GLSLSource += "#define ";
            GLSLSource += pMacro->Name;
            GLSLSource += ' ';
            GLSLSource += pMacro->Definition;
            GLSLSource += "\n";
            ++pMacro;
        }
    }

    RefCntAutoPtr<IDataBlob> pFileData(MakeNewRCObj<DataBlobImpl>()(0));
    auto ShaderSource = CreationAttribs.Source;
    size_t SourceLen = 0;
    if (ShaderSource)
    {
        SourceLen = strlen(ShaderSource);
    }
    else
    {
        VERIFY(CreationAttribs.pShaderSourceStreamFactory, "Input stream factory is null");
        RefCntAutoPtr<IFileStream> pSourceStream;
        CreationAttribs.pShaderSourceStreamFactory->CreateInputStream(CreationAttribs.FilePath, &pSourceStream);
        if (pSourceStream == nullptr)
            LOG_ERROR_AND_THROW("Failed to open shader source file");

        pSourceStream->Read(pFileData);
        ShaderSource = reinterpret_cast<char*>(pFileData->GetDataPtr());
        SourceLen = pFileData->GetSize();
    }

    if (CreationAttribs.SourceLanguage == SHADER_SOURCE_LANGUAGE_HLSL)
    {
        if (!CreationAttribs.UseCombinedTextureSamplers)
        {
            LOG_ERROR_AND_THROW("Combined texture samplers are required to convert HLSL source to GLSL");
        }
        // Convert HLSL to GLSL
        const auto &Converter = HLSL2GLSLConverterImpl::GetInstance();
        HLSL2GLSLConverterImpl::ConversionAttribs Attribs;
        Attribs.pSourceStreamFactory       = CreationAttribs.pShaderSourceStreamFactory;
        Attribs.ppConversionStream         = CreationAttribs.ppConversionStream;
        Attribs.HLSLSource                 = ShaderSource;
        Attribs.NumSymbols                 = SourceLen;
        Attribs.EntryPoint                 = CreationAttribs.EntryPoint;
        Attribs.ShaderType                 = CreationAttribs.Desc.ShaderType;
        Attribs.IncludeDefinitions         = true;
        Attribs.InputFileName              = CreationAttribs.FilePath;
        Attribs.SamplerSuffix              = CreationAttribs.CombinedSamplerSuffix;
        // Separate shader objects extension also allows input/output layout qualifiers for
        // all shader stages.
        // https://www.khronos.org/registry/OpenGL/extensions/ARB/ARB_separate_shader_objects.txt
        // (search for "Input Layout Qualifiers" and "Output Layout Qualifiers").
        Attribs.UseInOutLocationQualifiers = deviceCaps.bSeparableProgramSupported;
        auto ConvertedSource = Converter.Convert(Attribs);
        
        GLSLSource.append(ConvertedSource);
    }
    else
        GLSLSource.append(ShaderSource, SourceLen);

    return GLSLSource;
}

}
