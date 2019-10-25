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

/// \file
/// Routines that initialize D3D12-based engine implementation

#include "pch.h"
#include <array>
#include "EngineFactoryD3D12.h"
#include "RenderDeviceD3D12Impl.h"
#include "DeviceContextD3D12Impl.h"
#include "SwapChainD3D12Impl.h"
#include "D3D12TypeConversions.h"
#include "EngineFactoryD3DBase.h"
#include "StringTools.h"
#include "EngineMemory.h"
#include "CommandQueueD3D12Impl.h"
#include <Windows.h>
#include <dxgi1_4.h>

namespace Diligent
{

/// Engine factory for D3D12 implementation
class EngineFactoryD3D12Impl : public EngineFactoryD3DBase<IEngineFactoryD3D12, DeviceType::D3D12>
{
public:
    static EngineFactoryD3D12Impl* GetInstance()
    {
        static EngineFactoryD3D12Impl TheFactory;
        return &TheFactory;
    }

    using TBase = EngineFactoryD3DBase<IEngineFactoryD3D12, DeviceType::D3D12>;

    EngineFactoryD3D12Impl() :
        TBase{IID_EngineFactoryD3D12}
    {}

    void CreateDeviceAndContextsD3D12(const EngineD3D12CreateInfo&  EngineCI, 
                                      IRenderDevice**               ppDevice, 
                                      IDeviceContext**              ppContexts)override final;

    void AttachToD3D12Device(void*                        pd3d12NativeDevice, 
                             size_t                       CommandQueueCount,
                             ICommandQueueD3D12**         ppCommandQueues,
                             const EngineD3D12CreateInfo& EngineCI, 
                             IRenderDevice**              ppDevice, 
                             IDeviceContext**             ppContexts)override final;

    void CreateSwapChainD3D12( IRenderDevice*            pDevice, 
                               IDeviceContext*           pImmediateContext, 
                               const SwapChainDesc&      SwapChainDesc, 
                               const FullScreenModeDesc& FSDesc,
                               void*                     pNativeWndHandle, 
                               ISwapChain**              ppSwapChain )override final;
};

static void GetHardwareAdapter(IDXGIFactory2* pFactory, IDXGIAdapter1** ppAdapter, D3D_FEATURE_LEVEL FeatureLevel)
{
	CComPtr<IDXGIAdapter1> adapter;
	*ppAdapter = nullptr;

	for (UINT adapterIndex = 0; DXGI_ERROR_NOT_FOUND != pFactory->EnumAdapters1(adapterIndex, &adapter); ++adapterIndex, adapter.Release())
	{
		DXGI_ADAPTER_DESC1 desc;
		adapter->GetDesc1(&desc);

		if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
		{
			// Skip software devices
			continue;
		}

		// Check to see if the adapter supports Direct3D 12, but don't create the
		// actual device yet.
		if (SUCCEEDED(D3D12CreateDevice(adapter, FeatureLevel, _uuidof(ID3D12Device), nullptr)))
		{
			break;
		}
	}

	*ppAdapter = adapter.Detach();
}

void EngineFactoryD3D12Impl::CreateDeviceAndContextsD3D12(const EngineD3D12CreateInfo& EngineCI,
                                                          IRenderDevice**              ppDevice,
                                                          IDeviceContext**             ppContexts)
{
    if (EngineCI.DebugMessageCallback != nullptr)
        SetDebugMessageCallback(EngineCI.DebugMessageCallback);

    VERIFY( ppDevice && ppContexts, "Null pointer provided" );
    if( !ppDevice || !ppContexts )
        return;

    for (Uint32 Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; Type < D3D12_DESCRIPTOR_HEAP_TYPE_NUM_TYPES; ++Type)
    {
        auto CPUHeapAllocSize = EngineCI.CPUDescriptorHeapAllocationSize[Type];
        Uint32 MaxSize = 1 << 20;
        if( CPUHeapAllocSize > 1 << 20 )
        {
            LOG_ERROR( "CPU Heap allocation size is too large (", CPUHeapAllocSize, "). Max allowed size is ", MaxSize );
            return;
        }

        if( (CPUHeapAllocSize % 16) != 0 )
        {
            LOG_ERROR( "CPU Heap allocation size (", CPUHeapAllocSize, ") is expected to be multiple of 16" );
            return;
        }
    }

    *ppDevice = nullptr;
    memset(ppContexts, 0, sizeof(*ppContexts) * (1 + EngineCI.NumDeferredContexts));

    RefCntAutoPtr<CommandQueueD3D12Impl> pCmdQueueD3D12;
    CComPtr<ID3D12Device> d3d12Device;
    try
    {
	    // Enable the D3D12 debug layer.
        if (EngineCI.EnableDebugLayer)
	    {
		    CComPtr<ID3D12Debug> debugController;
		    if (SUCCEEDED(D3D12GetDebugInterface(__uuidof(debugController), reinterpret_cast<void**>(static_cast<ID3D12Debug**>(&debugController)) )))
		    {
			    debugController->EnableDebugLayer();
                //static_cast<ID3D12Debug1*>(debugController.p)->SetEnableSynchronizedCommandQueueValidation(FALSE);
		    }
	    }

	    CComPtr<IDXGIFactory4> factory;
        HRESULT hr = CreateDXGIFactory1(__uuidof(factory), reinterpret_cast<void**>(static_cast<IDXGIFactory4**>(&factory)) );
        CHECK_D3D_RESULT_THROW(hr, "Failed to create DXGI factory");

        // Direct3D12 does not allow feature levels below 11.0 (D3D12CreateDevice fails to create a device).
        const auto MinimumFeatureLevel = EngineCI.MinimumFeatureLevel >= DIRECT3D_FEATURE_LEVEL_11_0 ? EngineCI.MinimumFeatureLevel : DIRECT3D_FEATURE_LEVEL_11_0;

	    CComPtr<IDXGIAdapter1> hardwareAdapter;
        if(EngineCI.AdapterId == EngineD3D12CreateInfo::DefaultAdapterId)
        {
	        GetHardwareAdapter(factory, &hardwareAdapter, GetD3DFeatureLevel(MinimumFeatureLevel));
            if(hardwareAdapter == nullptr)
                LOG_ERROR_AND_THROW("No suitable hardware adapter found");
        }
        else
        {
            auto Adapters = FindCompatibleAdapters(MinimumFeatureLevel);
            if(EngineCI.AdapterId < Adapters.size())
                hardwareAdapter = Adapters[EngineCI.AdapterId];
            else
            {
                LOG_ERROR_AND_THROW(EngineCI.AdapterId, " is not a valid hardware adapter id. Total number of compatible adapters available on this system: ", Adapters.size());
            }
        }

        {
            DXGI_ADAPTER_DESC1 desc;
            hardwareAdapter->GetDesc1(&desc);
            LOG_INFO_MESSAGE("D3D12-capabale hardware found: ", NarrowString(desc.Description), " (", desc.DedicatedVideoMemory >> 20, " MB)");
        }

        constexpr auto MaxFeatureLevel = DIRECT3D_FEATURE_LEVEL_12_1;
        for (auto FeatureLevel = MaxFeatureLevel; FeatureLevel >= MinimumFeatureLevel; FeatureLevel = static_cast<DIRECT3D_FEATURE_LEVEL>(Uint8{FeatureLevel}-1))
        {
            auto d3dFeatureLevel = GetD3DFeatureLevel(FeatureLevel);
            hr = D3D12CreateDevice(hardwareAdapter, d3dFeatureLevel, __uuidof(d3d12Device), reinterpret_cast<void**>(static_cast<ID3D12Device**>(&d3d12Device)) );
            if (SUCCEEDED(hr))
            {
                VERIFY_EXPR(d3d12Device);
                break;
            }
        }
        if( FAILED(hr))
        {
            LOG_WARNING_MESSAGE("Failed to create hardware device. Attempting to create WARP device");

		    CComPtr<IDXGIAdapter> warpAdapter;
		    hr = factory->EnumWarpAdapter( __uuidof(warpAdapter),  reinterpret_cast<void**>(static_cast<IDXGIAdapter**>(&warpAdapter)) );
            CHECK_D3D_RESULT_THROW(hr, "Failed to enum warp adapter");

            for (auto FeatureLevel = MaxFeatureLevel; FeatureLevel >= MinimumFeatureLevel; FeatureLevel = static_cast<DIRECT3D_FEATURE_LEVEL>(Uint8{FeatureLevel}-1))
            {
                auto d3dFeatureLevel = GetD3DFeatureLevel(FeatureLevel);
		        hr = D3D12CreateDevice( warpAdapter, d3dFeatureLevel, __uuidof(d3d12Device), reinterpret_cast<void**>(static_cast<ID3D12Device**>(&d3d12Device)) );
                if (SUCCEEDED(hr))
                {
                    VERIFY_EXPR(d3d12Device);
                    break;
                }
            }
            CHECK_D3D_RESULT_THROW(hr, "Failed to crate warp device");
        }

        if (EngineCI.EnableDebugLayer)
        {
	        CComPtr<ID3D12InfoQueue> pInfoQueue;
            hr = d3d12Device->QueryInterface(__uuidof(pInfoQueue), reinterpret_cast<void**>(static_cast<ID3D12InfoQueue**>(&pInfoQueue)));
	        if( SUCCEEDED(hr) )
	        {
		        // Suppress whole categories of messages
		        //D3D12_MESSAGE_CATEGORY Categories[] = {};

		        // Suppress messages based on their severity level
		        D3D12_MESSAGE_SEVERITY Severities[] = 
		        {
			        D3D12_MESSAGE_SEVERITY_INFO
		        };

		        // Suppress individual messages by their ID
		        //D3D12_MESSAGE_ID DenyIds[] = {};

		        D3D12_INFO_QUEUE_FILTER NewFilter = {};
		        //NewFilter.DenyList.NumCategories = _countof(Categories);
		        //NewFilter.DenyList.pCategoryList = Categories;
		        NewFilter.DenyList.NumSeverities = _countof(Severities);
		        NewFilter.DenyList.pSeverityList = Severities;
		        //NewFilter.DenyList.NumIDs = _countof(DenyIds);
		        //NewFilter.DenyList.pIDList = DenyIds;

		        hr = pInfoQueue->PushStorageFilter(&NewFilter);
                VERIFY(SUCCEEDED(hr), "Failed to push storage filter");

                if (EngineCI.BreakOnCorruption)
                {
                    hr = pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, true);
                    VERIFY(SUCCEEDED(hr), "Failed to set break on corruption");
                }

                if (EngineCI.BreakOnError)
                {
                    hr = pInfoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR,      true);
                    VERIFY(SUCCEEDED(hr), "Failed to set break on error");
                }
            }
        }

#ifndef RELEASE
	    // Prevent the GPU from overclocking or underclocking to get consistent timings
	    //d3d12Device->SetStablePowerState(TRUE);
#endif

	    // Describe and create the command queue.
	    D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	    queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	    queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

        CComPtr<ID3D12CommandQueue> pd3d12CmdQueue;
        hr = d3d12Device->CreateCommandQueue(&queueDesc, __uuidof(pd3d12CmdQueue), reinterpret_cast<void**>(static_cast<ID3D12CommandQueue**>(&pd3d12CmdQueue)));
        CHECK_D3D_RESULT_THROW(hr, "Failed to create command queue");
        hr = pd3d12CmdQueue->SetName(L"Main Command Queue");
        VERIFY_EXPR(SUCCEEDED(hr));

        CComPtr<ID3D12Fence> pd3d12Fence;
        hr = d3d12Device->CreateFence(0, D3D12_FENCE_FLAG_NONE, __uuidof(pd3d12Fence), reinterpret_cast<void**>(static_cast<ID3D12Fence**>(&pd3d12Fence)));
        CHECK_D3D_RESULT_THROW(hr, "Failed to create main command queue fence");
	    d3d12Device->SetName(L"Main Command Queue fence");

        auto &RawMemAllocator = GetRawAllocator();
        pCmdQueueD3D12 = NEW_RC_OBJ(RawMemAllocator, "CommandQueueD3D12 instance", CommandQueueD3D12Impl)(pd3d12CmdQueue, pd3d12Fence);
    }
    catch( const std::runtime_error & )
    {
        LOG_ERROR( "Failed to initialize D3D12 resources" );
        return;
    }
        
    std::array<ICommandQueueD3D12*, 1> CmdQueues = {pCmdQueueD3D12};
    AttachToD3D12Device(d3d12Device, CmdQueues.size(), CmdQueues.data(), EngineCI, ppDevice, ppContexts);
}



void EngineFactoryD3D12Impl::AttachToD3D12Device(void*                        pd3d12NativeDevice, 
                                                 size_t                       CommandQueueCount,
                                                 ICommandQueueD3D12**         ppCommandQueues,
                                                 const EngineD3D12CreateInfo& EngineCI, 
                                                 IRenderDevice**              ppDevice, 
                                                 IDeviceContext**             ppContexts)
{
    if (EngineCI.DebugMessageCallback != nullptr)
        SetDebugMessageCallback(EngineCI.DebugMessageCallback);

    VERIFY( pd3d12NativeDevice && ppCommandQueues && ppDevice && ppContexts, "Null pointer provided" );
    if( !pd3d12NativeDevice || !ppCommandQueues || !ppDevice || !ppContexts )
        return;

    *ppDevice = nullptr;
    memset(ppContexts, 0, sizeof(*ppContexts) * (1 + EngineCI.NumDeferredContexts));

    try
    {
        SetRawAllocator(EngineCI.pRawMemAllocator);
        auto &RawMemAllocator = GetRawAllocator();
        auto d3d12Device = reinterpret_cast<ID3D12Device*>(pd3d12NativeDevice);
        RenderDeviceD3D12Impl *pRenderDeviceD3D12( NEW_RC_OBJ(RawMemAllocator, "RenderDeviceD3D12Impl instance", RenderDeviceD3D12Impl)(RawMemAllocator, this, EngineCI, d3d12Device, CommandQueueCount, ppCommandQueues) );
        pRenderDeviceD3D12->QueryInterface(IID_RenderDevice, reinterpret_cast<IObject**>(ppDevice) );

        RefCntAutoPtr<DeviceContextD3D12Impl> pImmediateCtxD3D12( NEW_RC_OBJ(RawMemAllocator, "DeviceContextD3D12Impl instance", DeviceContextD3D12Impl)(pRenderDeviceD3D12, false, EngineCI, 0, 0) );
        // We must call AddRef() (implicitly through QueryInterface()) because pRenderDeviceD3D12 will
        // keep a weak reference to the context
        pImmediateCtxD3D12->QueryInterface(IID_DeviceContext, reinterpret_cast<IObject**>(ppContexts) );
        pRenderDeviceD3D12->SetImmediateContext(pImmediateCtxD3D12);

        for (Uint32 DeferredCtx = 0; DeferredCtx < EngineCI.NumDeferredContexts; ++DeferredCtx)
        {
            RefCntAutoPtr<DeviceContextD3D12Impl> pDeferredCtxD3D12( NEW_RC_OBJ(RawMemAllocator, "DeviceContextD3D12Impl instance", DeviceContextD3D12Impl)(pRenderDeviceD3D12, true, EngineCI, 1+DeferredCtx, 0) );
            // We must call AddRef() (implicitly through QueryInterface()) because pRenderDeviceD3D12 will
            // keep a weak reference to the context
            pDeferredCtxD3D12->QueryInterface(IID_DeviceContext, reinterpret_cast<IObject**>(ppContexts + 1 + DeferredCtx) );
            pRenderDeviceD3D12->SetDeferredContext(DeferredCtx, pDeferredCtxD3D12);
        }
    }
    catch( const std::runtime_error & )
    {
        if( *ppDevice )
        {
            (*ppDevice)->Release();
            *ppDevice = nullptr;
        }
        for(Uint32 ctx=0; ctx < 1 + EngineCI.NumDeferredContexts; ++ctx)
        {
            if( ppContexts[ctx] != nullptr )
            {
                ppContexts[ctx]->Release();
                ppContexts[ctx] = nullptr;
            }
        }

        LOG_ERROR( "Failed to create device and contexts" );
    }
}


void EngineFactoryD3D12Impl::CreateSwapChainD3D12(IRenderDevice*            pDevice, 
                                                  IDeviceContext*           pImmediateContext, 
                                                  const SwapChainDesc&      SCDesc, 
                                                  const FullScreenModeDesc& FSDesc,
                                                  void*                     pNativeWndHandle, 
                                                  ISwapChain**              ppSwapChain )
{
    VERIFY( ppSwapChain, "Null pointer provided" );
    if( !ppSwapChain )
        return;

    *ppSwapChain = nullptr;

    try
    {
        auto* pDeviceD3D12 = ValidatedCast<RenderDeviceD3D12Impl>( pDevice );
        auto* pDeviceContextD3D12 = ValidatedCast<DeviceContextD3D12Impl>(pImmediateContext);
        auto& RawMemAllocator = GetRawAllocator();

        if (pDeviceContextD3D12->GetSwapChain() != nullptr && SCDesc.IsPrimary)
        {
            LOG_ERROR_AND_THROW("Another swap chain labeled as primary has already been created. "
                                "There must only be one primary swap chain.");
        }

        auto* pSwapChainD3D12 = NEW_RC_OBJ(RawMemAllocator, "SwapChainD3D12Impl instance", SwapChainD3D12Impl)(SCDesc, FSDesc, pDeviceD3D12, pDeviceContextD3D12, pNativeWndHandle);
        pSwapChainD3D12->QueryInterface( IID_SwapChain, reinterpret_cast<IObject**>(ppSwapChain) );

        if (SCDesc.IsPrimary)
        {
            pDeviceContextD3D12->SetSwapChain(pSwapChainD3D12);
            // Bind default render target
            pDeviceContextD3D12->SetRenderTargets( 0, nullptr, nullptr, RESOURCE_STATE_TRANSITION_MODE_TRANSITION );
            // Set default viewport
            pDeviceContextD3D12->SetViewports( 1, nullptr, 0, 0 );
        
            auto NumDeferredCtx = pDeviceD3D12->GetNumDeferredContexts();
            for (size_t ctx = 0; ctx < NumDeferredCtx; ++ctx)
            {
                if (auto pDeferredCtx = pDeviceD3D12->GetDeferredContext(ctx))
                {
                    auto* pDeferredCtxD3D12 = pDeferredCtx.RawPtr<DeviceContextD3D12Impl>();
                    pDeferredCtxD3D12->SetSwapChain(pSwapChainD3D12);
                    // We cannot bind default render target here because
                    // there is no guarantee that deferred context will be used
                    // in this frame. It is an error to bind 
                    // RTV of an inactive buffer in the swap chain
                }
            }
        }
    }
    catch( const std::runtime_error& )
    {
        if( *ppSwapChain )
        {
            (*ppSwapChain)->Release();
            *ppSwapChain = nullptr;
        }

        LOG_ERROR( "Failed to create the swap chain" );
    }
}


#ifdef DOXYGEN
/// Loads Direct3D12-based engine implementation and exports factory functions
/// \param [out] GetFactoryFunc - Pointer to the function that returns factory for D3D12 engine implementation.
///                               See EngineFactoryD3D12Impl.
/// \remarks Depending on the configuration and platform, the function loads different dll:
/// Platform\\Configuration   |           Debug               |        Release
/// --------------------------|-------------------------------|----------------------------
///         x86               | GraphicsEngineD3D12_32d.dll   |    GraphicsEngineD3D12_32r.dll
///         x64               | GraphicsEngineD3D12_64d.dll   |    GraphicsEngineD3D12_64r.dll
///
void LoadGraphicsEngineD3D12(GetEngineFactoryD3D12Type& GetFactoryFunc)
{
    // This function is only required because DoxyGen refuses to generate documentation for a static function when SHOW_FILES==NO
    #error This function must never be compiled;    
}
#endif


IEngineFactoryD3D12* GetEngineFactoryD3D12()
{
    return EngineFactoryD3D12Impl::GetInstance();
}

}
