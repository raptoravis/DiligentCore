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

#include "../../External/vulkan/vulkan.h"
#include <mutex>
#include "stl/unordered_map.h"
#include "stl/deque.h"
#include "stl/vector.h"

#include "TextureUploaderVk.h"
#include "RenderDeviceVk.h"
#include "BufferVk.h"
#include "TextureVk.h"
#include "ThreadSignal.h"

namespace Diligent
{
    class UploadBufferVk : public UploadBufferBase
    {
    public:
        UploadBufferVk(IReferenceCounters *pRefCounters, 
                       IRenderDeviceVk *pRenderDeviceVk,
                       const UploadBufferDesc &Desc, 
                       IBuffer *pStagingBuffer,
                       void *pData, 
                       size_t RowStride, 
                       size_t DepthStride) :
            UploadBufferBase(pRefCounters, Desc),
            m_pDeviceVk(pRenderDeviceVk),
            m_pStagingBuffer(pStagingBuffer)
        {
            m_pData = pData;
            m_RowStride = RowStride;
            m_DepthStride = DepthStride;
        }

        ~UploadBufferVk()
        {
#if 0
            RefCntAutoPtr<IBufferVk> pBufferVk(m_pStagingBuffer, IID_BufferVk);
            size_t DataStartOffset;
            auto* pVkBuff = pBufferVk->GetVkBuffer(DataStartOffset, nullptr);
            pVkBuff->Unmap(0, nullptr);
            LOG_INFO_MESSAGE("Releasing staging buffer of size ", m_pStagingBuffer->GetDesc().uiSizeInBytes);
#endif
        }
          
        void SignalCopyScheduled()
        {
            m_CopyScheduledSignal.Trigger();
        }

        void Reset()
        {
            m_CopyScheduledSignal.Reset();
        }

        virtual void WaitForCopyScheduled()override final
        {
            m_CopyScheduledSignal.Wait();
        }

        IBuffer* GetStagingBuffer() { return m_pStagingBuffer; }

        bool DbgIsCopyScheduled()const { return m_CopyScheduledSignal.IsTriggered(); }
    private:

        ThreadingTools::Signal m_CopyScheduledSignal;

        RefCntAutoPtr<IBuffer> m_pStagingBuffer;
        RefCntAutoPtr<IRenderDeviceVk> m_pDeviceVk;
    };

    struct TextureUploaderVk::InternalData
    {
        InternalData(IRenderDevice *pDevice) :
            m_pDeviceVk(pDevice, IID_RenderDeviceVk)
        {
            //m_pVkNativeDevice = m_pDeviceVk->GetVkDevice();
        }

        //CComPtr<IVkDevice> m_pVkNativeDevice;
        RefCntAutoPtr<IRenderDeviceVk> m_pDeviceVk;

        void SwapMapQueues()
        {
            std::lock_guard<std::mutex> QueueLock(m_PendingOperationsMtx);
            m_PendingOperations.swap(m_InWorkOperations);
        }

        void EnqueCopy(UploadBufferVk *pUploadBuffer, ITextureVk *pDstTex, Uint32 dstSlice, Uint32 dstMip)
        {
            std::lock_guard<std::mutex> QueueLock(m_PendingOperationsMtx);
            m_PendingOperations.emplace_back(PendingBufferOperation::Operation::Copy, pUploadBuffer, pDstTex, dstSlice, dstMip);
        }

        std::mutex m_PendingOperationsMtx;
        struct PendingBufferOperation
        {
            enum Operation
            {
                Copy
            }operation;
            RefCntAutoPtr<UploadBufferVk> pUploadBuffer;
            RefCntAutoPtr<ITextureVk> pDstTexture;
            Uint32 DstSlice = 0;
            Uint32 DstMip = 0;

            PendingBufferOperation(Operation op, UploadBufferVk* pBuff) :
                operation(op),
                pUploadBuffer(pBuff)
            {}
            PendingBufferOperation(Operation op, UploadBufferVk* pBuff, ITextureVk *pDstTex, Uint32 dstSlice, Uint32 dstMip) :
                operation(op),
                pUploadBuffer(pBuff),
                pDstTexture(pDstTex),
                DstSlice(dstSlice),
                DstMip(dstMip)
            {}
        };
        stl::vector< PendingBufferOperation > m_PendingOperations;
        stl::vector< PendingBufferOperation > m_InWorkOperations;

        std::mutex m_UploadBuffCacheMtx;
        stl::unordered_map< UploadBufferDesc, stl::deque< stl::pair<Uint64, RefCntAutoPtr<UploadBufferVk> > > > m_UploadBufferCache;
    };

    TextureUploaderVk::TextureUploaderVk(IReferenceCounters *pRefCounters, IRenderDevice *pDevice, const TextureUploaderDesc Desc) :
        TextureUploaderBase(pRefCounters, pDevice, Desc),
        m_pInternalData(new InternalData(pDevice))
    {
    }

    TextureUploaderVk::~TextureUploaderVk()
    {
        for (auto BuffQueueIt : m_pInternalData->m_UploadBufferCache)
        {
            if (BuffQueueIt.second.size())
            {
                const auto &desc = BuffQueueIt.first;
                auto &FmtInfo = m_pDevice->GetTextureFormatInfo(desc.Format);
                LOG_INFO_MESSAGE("TextureUploaderVk: releasing ", BuffQueueIt.second.size(), ' ', desc.Width, 'x', desc.Height, 'x', desc.Depth, ' ', FmtInfo.Name, " upload buffer(s) ");
            }
        }
    }

    void TextureUploaderVk::RenderThreadUpdate(IDeviceContext *pContext)
    {
        m_pInternalData->SwapMapQueues();
        if (!m_pInternalData->m_InWorkOperations.empty())
        {
            for (auto &OperationInfo : m_pInternalData->m_InWorkOperations)
            {
                auto &pBuffer = OperationInfo.pUploadBuffer;

                switch (OperationInfo.operation)
                {
                    case InternalData::PendingBufferOperation::Copy:
                    {
                        TextureSubResData SubResData(pBuffer->GetStagingBuffer(), 0, static_cast<Uint32>(pBuffer->GetRowStride()));
                        const auto &TexDesc = OperationInfo.pDstTexture->GetDesc();
                        Box DstBox;
                        DstBox.MaxX = TexDesc.Width;
                        DstBox.MaxY = TexDesc.Height;
                        // UpdateTexture() transitions dst subresource to COPY_DEST state and then transitions back to original state
#if 0
                        pContext->UpdateTexture(OperationInfo.pDstTexture, OperationInfo.DstMip, OperationInfo.DstSlice, DstBox,
                                                SubResData, RESOURCE_STATE_TRANSITION_MODE_TRANSITION, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
#endif
                        pBuffer->SignalCopyScheduled();
                    }
                    break;
                }
            }

            m_pInternalData->m_InWorkOperations.clear();
        }
    }

    void TextureUploaderVk::AllocateUploadBuffer(const UploadBufferDesc& Desc, bool IsRenderThread, IUploadBuffer **ppBuffer)
    {
        *ppBuffer = nullptr;

        {
            std::lock_guard<std::mutex> CacheLock(m_pInternalData->m_UploadBuffCacheMtx);
            auto &Cache = m_pInternalData->m_UploadBufferCache;
            if (!Cache.empty())
            {
                auto DequeIt = Cache.find(Desc);
                if (DequeIt != Cache.end())
                {
                    auto &Deque = DequeIt->second;
                    if (!Deque.empty())
                    {
                        auto &FrontBuff = Deque.front();
                        if (m_pInternalData->m_pDeviceVk->IsFenceSignaled(0, FrontBuff.first))
                        {
                            *ppBuffer = FrontBuff.second.Detach();
                            Deque.pop_front();
                        }
                    }
                }
            }
        }

        // No available buffer found in the cache
        if(*ppBuffer == nullptr)
        {
            BufferDesc BuffDesc;
            BuffDesc.Name = "Staging buffer for UploadBufferVk";
            BuffDesc.CPUAccessFlags = CPU_ACCESS_WRITE;
            BuffDesc.Usage = USAGE_CPU_ACCESSIBLE;

            const auto &TexFmtInfo = m_pDevice->GetTextureFormatInfo(Desc.Format);
            Uint32 RowStride = Desc.Width * Uint32{TexFmtInfo.ComponentSize} * Uint32{TexFmtInfo.NumComponents};
            //static_assert((Vk_TEXTURE_DATA_PITCH_ALIGNMENT & (Vk_TEXTURE_DATA_PITCH_ALIGNMENT - 1)) == 0, "Alginment is expected to be power of 2");
            Uint32 AlignmentMask = 512;//Vk_TEXTURE_DATA_PITCH_ALIGNMENT-1;
            RowStride = (RowStride + AlignmentMask) & (~AlignmentMask);

            BuffDesc.uiSizeInBytes = Desc.Height * RowStride;
            RefCntAutoPtr<IBuffer> pStagingBuffer;
            m_pDevice->CreateBuffer(BuffDesc, BufferData(), &pStagingBuffer);

            PVoid CpuVirtualAddress = nullptr;
            RefCntAutoPtr<IBufferVk> pStagingBufferVk(pStagingBuffer, IID_BufferVk);
            size_t DataStartOffset;
#if 0
            auto* pVkBuff = pStagingBufferVk->GetVkBuffer(DataStartOffset, nullptr);
            pVkBuff->Map(0, nullptr, &CpuVirtualAddress);
#else
            static Uint8 DummyData[1<<20];
            CpuVirtualAddress = DummyData;
#endif
            if (CpuVirtualAddress == nullptr)
            {
                LOG_ERROR_MESSAGE("Failed to map upload buffer");
                return;
            }

            LOG_INFO_MESSAGE("Created staging buffer of size ", BuffDesc.uiSizeInBytes);

            RefCntAutoPtr<UploadBufferVk> pUploadBuffer(MakeNewRCObj<UploadBufferVk>()(m_pInternalData->m_pDeviceVk, Desc, pStagingBuffer, CpuVirtualAddress, RowStride, 0));
            *ppBuffer = pUploadBuffer.Detach();
        }
    }

    void TextureUploaderVk::ScheduleGPUCopy(ITexture *pDstTexture,
        Uint32 ArraySlice,
        Uint32 MipLevel,
        IUploadBuffer *pUploadBuffer)
    {
        auto *pUploadBufferVk = ValidatedCast<UploadBufferVk>(pUploadBuffer);
        RefCntAutoPtr<ITextureVk> pDstTexVk(pDstTexture, IID_TextureVk);
        m_pInternalData->EnqueCopy(pUploadBufferVk, pDstTexVk, ArraySlice, MipLevel);
    }

    void TextureUploaderVk::RecycleBuffer(IUploadBuffer *pUploadBuffer)
    {
        auto *pUploadBufferVk = ValidatedCast<UploadBufferVk>(pUploadBuffer);
        VERIFY(pUploadBufferVk->DbgIsCopyScheduled(), "Upload buffer must be recycled only after copy operation has been scheduled on the GPU");
        pUploadBufferVk->Reset();

        std::lock_guard<std::mutex> CacheLock(m_pInternalData->m_UploadBuffCacheMtx);
        auto &Cache = m_pInternalData->m_UploadBufferCache;
        auto &Deque = Cache[pUploadBufferVk->GetDesc()];
        Uint64 FenceValue = m_pInternalData->m_pDeviceVk->GetNextFenceValue(0);
        Deque.emplace_back( FenceValue, pUploadBufferVk );
    }
}
