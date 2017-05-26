/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORTEXTURESPARENT_H
#define MOZILLA_GFX_COMPOSITORTEXTURESPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/layers/PCompositorTexturesParent.h"
#include "mozilla/layers/ISurfaceAllocator.h"

namespace mozilla {
namespace layers {

class CompositorTexturesParent final : public PCompositorTexturesParent
                                     , public HostIPCAllocator
                                     , public ShmemAllocator
{
public:
  CompositorTexturesParent();

  void ActorDestroy(ActorDestroyReason aReason) override;

  ipc::IPCResult Recv__delete__() override;

  PTextureParent* AllocPTextureParent(const SurfaceDescriptor& aSharedData,
                                      const LayersBackend& aLayersBackend,
                                      const TextureFlags& aFlags,
                                      const uint64_t& aId,
                                      const uint64_t& aSerial,
                                      const wr::MaybeExternalImageId& aExternalImageId) override;

  bool DeallocPTextureParent(PTextureParent* actor) override;

  // ISurfaceAllocator (HostIPCAllocator)
  ShmemAllocator* AsShmemAllocator() override { return this; }
  bool UsesCompositorTextures() const override { return true; }
  bool IPCOpen() const override { return mCanSend; }
  bool IsSameProcess() const override;

  // HostIPCAllocator
  base::ProcessId GetChildProcessId() override;
  void NotifyNotUsed(PTextureParent* aTexture, uint64_t aTransactionId) override;
  void SendAsyncMessage(const InfallibleTArray<AsyncParentMessageData>& aMessage) override;

  // ShmemAllocator
  bool AllocShmem(size_t aSize,
                  ipc::SharedMemory::SharedMemoryType aShmType,
                  ipc::Shmem* aShmem) override;
  bool AllocUnsafeShmem(size_t aSize,
                        ipc::SharedMemory::SharedMemoryType aShmType,
                        ipc::Shmem* aShmem) override;
  void DeallocShmem(ipc::Shmem& aShmem) override;

private:
  ~CompositorTexturesParent() override;

  bool mCanSend;
};

} // namespace layers
} // namespace mozilla

#endif
