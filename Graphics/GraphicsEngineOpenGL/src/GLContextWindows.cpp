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

#include "GLContextWindows.h"
#include "DeviceCaps.h"
#include "GLTypeConversions.h"
#include "GraphicsAccessories.h"

namespace Diligent
{

    void APIENTRY openglCallbackFunction( GLenum source,
        GLenum type,
        GLuint id,
        GLenum severity,
        GLsizei length,
        const GLchar* message,
        const void* userParam )
    {
        // Note: disabling flood of notifications through glDebugMessageControl() has no effect, 
        // so we have to filter them out here
        if (id == 131185 || // Buffer detailed info: Buffer object <X> (bound to GL_XXXX ... , usage hint is GL_DYNAMIC_DRAW) 
                            // will use VIDEO memory as the source for buffer object operations.
            id == 131186 // Buffer object <X> (bound to GL_XXXX, usage hint is GL_DYNAMIC_DRAW) is being copied/moved from VIDEO memory to HOST memory.
            )
            return;

        std::stringstream MessageSS;

        MessageSS << "OpenGL debug message " << id << " (";
        switch (source)
        {
            case GL_DEBUG_SOURCE_API:             MessageSS << "Source: API.";             break;
            case GL_DEBUG_SOURCE_WINDOW_SYSTEM:   MessageSS << "Source: Window System.";   break;
            case GL_DEBUG_SOURCE_SHADER_COMPILER: MessageSS << "Source: Shader Compiler."; break;
            case GL_DEBUG_SOURCE_THIRD_PARTY:     MessageSS << "Source: Third Party.";     break;
            case GL_DEBUG_SOURCE_APPLICATION:     MessageSS << "Source: Application.";     break;
            case GL_DEBUG_SOURCE_OTHER:           MessageSS << "Source: Other.";           break;
            default:                              MessageSS << "Source: Unknown (" << source << ").";
        }

        switch (type)
        {
            case GL_DEBUG_TYPE_ERROR:               MessageSS << " Type: ERROR.";                break;
            case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR: MessageSS << " Type: Deprecated Behaviour."; break;
            case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:  MessageSS << " Type: UNDEFINED BEHAVIOUR.";  break; 
            case GL_DEBUG_TYPE_PORTABILITY:         MessageSS << " Type: Portability.";          break;
            case GL_DEBUG_TYPE_PERFORMANCE:         MessageSS << " Type: PERFORMANCE.";          break;
            case GL_DEBUG_TYPE_MARKER:              MessageSS << " Type: Marker.";               break;
            case GL_DEBUG_TYPE_PUSH_GROUP:          MessageSS << " Type: Push Group.";           break;
            case GL_DEBUG_TYPE_POP_GROUP:           MessageSS << " Type: Pop Group.";            break;
            case GL_DEBUG_TYPE_OTHER:               MessageSS << " Type: Other.";                break;
            default:                                MessageSS << " Type: Unknown (" << type << ").";
        }

        switch (severity)
        {
            case GL_DEBUG_SEVERITY_HIGH:         MessageSS << " Severity: HIGH";         break;
            case GL_DEBUG_SEVERITY_MEDIUM:       MessageSS << " Severity: Medium";       break;
            case GL_DEBUG_SEVERITY_LOW:          MessageSS << " Severity: Low";          break;
            case GL_DEBUG_SEVERITY_NOTIFICATION: MessageSS << " Severity: Notification"; break;
            default:                             MessageSS << " Severity: Unknown (" << severity << ")"; break;
        }

        MessageSS << "): " << message;

        LOG_INFO_MESSAGE( MessageSS.str().c_str() );
    }

    GLContext::GLContext(const EngineGLCreateInfo& InitAttribs, DeviceCaps& deviceCaps, const SwapChainDesc* pSCDesc) :
		m_Context                    {0},
		m_WindowHandleToDeviceContext{0}
    {
		Int32 MajorVersion = 0, MinorVersion = 0;
		if(InitAttribs.pNativeWndHandle != nullptr)
		{
			HWND hWnd = reinterpret_cast<HWND>(InitAttribs.pNativeWndHandle);

			// See http://www.opengl.org/wiki/Tutorial:_OpenGL_3.1_The_First_Triangle_(C%2B%2B/Win)
			//     http://www.opengl.org/wiki/Creating_an_OpenGL_Context_(WGL)
			PIXELFORMATDESCRIPTOR pfd;
			memset( &pfd, 0, sizeof( PIXELFORMATDESCRIPTOR ) );
			pfd.nSize = sizeof( PIXELFORMATDESCRIPTOR );
			pfd.nVersion = 1;
			pfd.dwFlags = PFD_DOUBLEBUFFER | PFD_SUPPORT_OPENGL | PFD_DRAW_TO_WINDOW;
			pfd.iPixelType = PFD_TYPE_RGBA;
            if (pSCDesc != nullptr)
            {
                auto ColorFmt = pSCDesc->ColorBufferFormat;
                if (ColorFmt == TEX_FORMAT_RGBA8_UNORM || ColorFmt == TEX_FORMAT_RGBA8_UNORM_SRGB || 
                    ColorFmt == TEX_FORMAT_BGRA8_UNORM || ColorFmt == TEX_FORMAT_BGRA8_UNORM_SRGB)
                {
                    pfd.cColorBits = 32;
                }
                else
                {
                    LOG_WARNING_MESSAGE("Unsupported color buffer format ", GetTextureFormatAttribs(ColorFmt).Name, ". "
                                        "OpenGL only supports 32-bit UNORM color buffer formats.");
                    pfd.cColorBits = 32;
                }

                auto DepthFmt = pSCDesc->DepthBufferFormat;
                switch(DepthFmt)
                {
                    case TEX_FORMAT_D32_FLOAT_S8X24_UINT:
                        pfd.cDepthBits   = 32;
                        pfd.cStencilBits = 8;
                    break;

                    case TEX_FORMAT_D32_FLOAT:
                        pfd.cDepthBits = 32;
                    break;

                    case TEX_FORMAT_D24_UNORM_S8_UINT:
                        pfd.cDepthBits   = 24;
                        pfd.cStencilBits = 8;
                    break;

                    case TEX_FORMAT_D16_UNORM:
                        pfd.cDepthBits = 16;
                    break;

                    default:
                        LOG_ERROR_MESSAGE("Unsupported depth buffer format ", GetTextureFormatAttribs(DepthFmt).Name);
                        pfd.cDepthBits = 32;
                }
            }
            else
            {
    			pfd.cColorBits = 32;
	    		pfd.cDepthBits = 32;
            }
			pfd.iLayerType = PFD_MAIN_PLANE;

			m_WindowHandleToDeviceContext = GetDC( hWnd );
			int nPixelFormat = ChoosePixelFormat( m_WindowHandleToDeviceContext, &pfd );

			if( nPixelFormat == 0 )
				LOG_ERROR_AND_THROW( "Invalid Pixel Format" );

			BOOL bResult = SetPixelFormat( m_WindowHandleToDeviceContext, nPixelFormat, &pfd );
			if( !bResult )
				LOG_ERROR_AND_THROW( "Failed to set Pixel Format" );

			// Create standard OpenGL (2.1) rendering context which will be used only temporarily, 
			HGLRC tempContext = wglCreateContext( m_WindowHandleToDeviceContext );
			// and make it current
			wglMakeCurrent( m_WindowHandleToDeviceContext, tempContext );

			// Initialize GLEW
			GLenum err = glewInit();
			if( GLEW_OK != err )
				LOG_ERROR_AND_THROW( "Failed to initialize GLEW" );
        
			if (wglewIsSupported( "WGL_ARB_create_context" ) == 1)
			{
                std::pair<int,int> gl_versions[] = {{4,4}, {4,3}, {4,2}};
                for(size_t i=0; i < _countof(gl_versions) && m_Context == NULL; ++i)
                {
                    const auto& version = gl_versions[i];
				    MajorVersion = version.first;
				    MinorVersion = version.second;
				    // Setup attributes for a new OpenGL rendering context
				    int attribs[] =
				    {
					    WGL_CONTEXT_MAJOR_VERSION_ARB, MajorVersion,
					    WGL_CONTEXT_MINOR_VERSION_ARB, MinorVersion,
					    WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_FORWARD_COMPATIBLE_BIT_ARB,
					    GL_CONTEXT_PROFILE_MASK, GL_CONTEXT_CORE_PROFILE_BIT,
					    0, 0
				    };

#ifdef _DEBUG
				    attribs[5] |= WGL_CONTEXT_DEBUG_BIT_ARB;
#endif 

				    // Create new rendering context
				    // In order to create new OpenGL rendering context we have to call function wglCreateContextAttribsARB(), 
				    // which is an OpenGL function and requires OpenGL to be active when it is called. 
				    // The only way is to create an old context, activate it, and while it is active create a new one. 
				    // Very inconsistent, but we have to live with it!
				    m_Context = wglCreateContextAttribsARB( m_WindowHandleToDeviceContext, 0, attribs );
                }

                if (m_Context == NULL)
                {
                    LOG_ERROR_AND_THROW("Failed to initialize OpenGL context.");
                }

				// Delete tempContext
				wglMakeCurrent( NULL, NULL );
				wglDeleteContext( tempContext );
				// Make new context current
				wglMakeCurrent( m_WindowHandleToDeviceContext, m_Context );
				wglSwapIntervalEXT( 0 );
			}
			else
			{       //It's not possible to make a GL 4.x context. Use the old style context (GL 2.1 and before)
				m_Context = tempContext;
			}

            if( glDebugMessageCallback )
            {
                glEnable( GL_DEBUG_OUTPUT_SYNCHRONOUS );
                glDebugMessageCallback( openglCallbackFunction, nullptr );
                GLuint unusedIds = 0;
                glDebugMessageControl(
                    GL_DONT_CARE, // Source of debug messages to enable or disable
                    GL_DONT_CARE, // Type of debug messages to enable or disable
                    GL_DONT_CARE, // Severity of debug messages to enable or disable
                    0,            // The length of the array ids
                    &unusedIds,   // Array of unsigned integers contianing the ids of the messages to enable or disable
                    GL_TRUE       // Flag determining whether the selected messages should be enabled or disabled
                );
                if( glGetError() != GL_NO_ERROR )
                    LOG_ERROR_MESSAGE("Failed to enable debug messages");
            }
		}
		else
		{
			auto CurrentCtx = wglGetCurrentContext();
			if (CurrentCtx == 0)
			{
                LOG_ERROR_AND_THROW("No current GL context found! Provide non-null handle to a native Window to create a GL context");
			}
			
			// Initialize GLEW
			GLenum err = glewInit();
			if( GLEW_OK != err )
				LOG_ERROR_AND_THROW( "Failed to initialize GLEW" );
		}

        //Checking GL version
        const GLubyte *GLVersionString = glGetString( GL_VERSION );

        //Or better yet, use the GL3 way to get the version number
        glGetIntegerv( GL_MAJOR_VERSION, &MajorVersion );
        glGetIntegerv( GL_MINOR_VERSION, &MinorVersion );
        LOG_INFO_MESSAGE(InitAttribs.pNativeWndHandle != nullptr ? "Initialized OpenGL " : "Attached to OpenGL ", MajorVersion, '.', MinorVersion, " context (", GLVersionString, ')');

        // Under the standard filtering rules for cubemaps, filtering does not work across faces of the cubemap. 
        // This results in a seam across the faces of a cubemap. This was a hardware limitation in the past, but 
        // modern hardware is capable of interpolating across a cube face boundary.
        // GL_TEXTURE_CUBE_MAP_SEAMLESS is not defined in OpenGLES
        glEnable(GL_TEXTURE_CUBE_MAP_SEAMLESS);
        if( glGetError() != GL_NO_ERROR )
            LOG_ERROR_MESSAGE("Failed to enable seamless cubemap filtering");

        // When GL_FRAMEBUFFER_SRGB is enabled, and if the destination image is in the sRGB colorspace
        // then OpenGL will assume the shader's output is in the linear RGB colorspace. It will therefore 
        // convert the output from linear RGB to sRGB.
        // Any writes to images that are not in the sRGB format should not be affected.
        // Thus this setting should be just set once and left that way
        glEnable(GL_FRAMEBUFFER_SRGB);
        if( glGetError() != GL_NO_ERROR )
            LOG_ERROR_MESSAGE("Failed to enable SRGB framebuffers");

        deviceCaps.DevType = DeviceType::OpenGL;
        deviceCaps.MajorVersion = MajorVersion;
        deviceCaps.MinorVersion = MinorVersion;
        bool IsGL43OrAbove = MajorVersion >= 5 || MajorVersion == 4 && MinorVersion >= 3;
        auto &TexCaps = deviceCaps.TexCaps;
        TexCaps.bTexture2DMSSupported      = IsGL43OrAbove;
        TexCaps.bTexture2DMSArraySupported = IsGL43OrAbove;
        TexCaps.bTextureViewSupported      = IsGL43OrAbove;
        TexCaps.bCubemapArraysSupported    = IsGL43OrAbove;
        deviceCaps.bMultithreadedResourceCreationSupported = False;
    }

    GLContext::~GLContext()
    {
		// Do not destroy context if it was created by the app.
        if( m_Context )
		{
			wglMakeCurrent( m_WindowHandleToDeviceContext, 0 );
            wglDeleteContext( m_Context );
		}
    }

    void GLContext::SwapBuffers()
    {
        if (m_WindowHandleToDeviceContext)
            ::SwapBuffers(m_WindowHandleToDeviceContext);
        else
            LOG_ERROR("Swap buffer failed because window handle to device context is not initialized");
    }

    GLContext::NativeGLContextType GLContext::GetCurrentNativeGLContext()
    {
        return wglGetCurrentContext();
    }
}
