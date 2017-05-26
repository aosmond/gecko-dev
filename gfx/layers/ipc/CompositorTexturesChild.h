/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORTEXTURESCHILD_H
#define MOZILLA_GFX_COMPOSITORTEXTURESCHILD_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/layers/PCompositorTexturesChild.h"
#include "mozilla/layers/TextureForwarder.h" // for TextureForwarder

namespace mozilla {
namespace layers {

class CompositorTexturesChild final : public PCompositorTexturesChild
                                    , public TextureForwarder
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorTexturesChild, override)

public:
  CompositorTexturesChild();

  void ActorDestroy(ActorDestroyReason aReason) override;

  ipc::IPCResult
  RecvParentAsyncMessages(InfallibleTArray<AsyncParentMessageData>&& aMessages) override;

  PTextureChild* AllocPTextureChild(const SurfaceDescriptor& aSharedData,
                                    const LayersBackend& aLayersBackend,
                                    const TextureFlags& aFlags,
                                    const uint64_t& aId,
                                    const uint64_t& aSerial,
                                    const wr::MaybeExternalImageId& aExternalImageId) override;

  bool DeallocPTextureChild(PTextureChild* actor) override;

  // LayersIPCActor (LayersIPCChannel)
  bool IPCOpen() const override { return mCanSend; }

  // IShmemAllocator (LayersIPCChannel)
  bool AllocShmem(size_t aSize,
                  ipc::SharedMemory::SharedMemoryType aShmType,
                  ipc::Shmem* aShmem) override;
  bool AllocUnsafeShmem(size_t aSize,
                        ipc::SharedMemory::SharedMemoryType aShmType,
                        ipc::Shmem* aShmem) override;
  bool DeallocShmem(ipc::Shmem& aShmem) override;

  // LayersIPCChannel (TextureForwarder)
  bool IsSameProcess() const override;
  base::ProcessId GetParentPid() const override;
  MessageLoop* GetMessageLoop() const override;
  FixedSizeSmallShmemSectionAllocator* GetTileLockAllocator() override;
  void CancelWaitForRecycle(uint64_t aTextureId) override;
  wr::MaybeExternalImageId GetNextExternalImageId() override;

  // TextureForwarder
  PTextureChild* CreateTexture(const SurfaceDescriptor& aSharedData,
                               LayersBackend aLayersBackend,
                               TextureFlags aFlags,
                               uint64_t aSerial,
                               wr::MaybeExternalImageId& aExternalImageId,
                               nsIEventTarget* aTarget = nullptr) override;

private:
  ~CompositorTexturesChild() override;

  MessageLoop* mMessageLoop;
  FixedSizeSmallShmemSectionAllocator* mSectionAllocator;
  bool mCanSend;
};

} // namespace layers
} // namespace mozilla

#endif
