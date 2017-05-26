/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorTexturesParent.h"
#include "mozilla/layers/TextureHost.h"

namespace mozilla {
namespace layers {

CompositorTexturesParent::CompositorTexturesParent()
  : mCanSend(true)
{ }

CompositorTexturesParent::~CompositorTexturesParent()
{ }

void
CompositorTexturesParent::ActorDestroy(ActorDestroyReason aReason)
{
  mCanSend = false;
}

ipc::IPCResult
CompositorTexturesParent::Recv__delete__()
{
  return IPC_OK();
}

PTextureParent*
CompositorTexturesParent::AllocPTextureParent(const SurfaceDescriptor& aSharedData,
                                            const LayersBackend& aLayersBackend,
                                            const TextureFlags& aFlags,
                                            const uint64_t& aId,
                                            const uint64_t& aSerial,
                                            const wr::MaybeExternalImageId& aExternalImageId)
{
  return TextureHost::CreateIPDLActor(/*this*/ nullptr, aSharedData, aLayersBackend, aFlags, aSerial, aExternalImageId);;
}

bool
CompositorTexturesParent::DeallocPTextureParent(PTextureParent* actor)
{
  return TextureHost::DestroyIPDLActor(actor);
}

bool
CompositorTexturesParent::IsSameProcess() const
{
  return OtherPid() == base::GetCurrentProcId();
}

base::ProcessId
CompositorTexturesParent::GetChildProcessId()
{
  return OtherPid();
}

void
CompositorTexturesParent::NotifyNotUsed(PTextureParent* aTexture, uint64_t aTransactionId)
{
  RefPtr<TextureHost> texture = TextureHost::AsTextureHost(aTexture);
  if (!texture) {
    return;
  }

  if (!(texture->GetFlags() & TextureFlags::RECYCLE)) {
    return;
  }

  uint64_t textureId = TextureHost::GetTextureSerial(aTexture);
  mPendingAsyncMessage.push_back(
    OpNotifyNotUsed(textureId, aTransactionId));
}

void
CompositorTexturesParent::SendAsyncMessage(const InfallibleTArray<AsyncParentMessageData>& aMessage)
{
  Unused << SendParentAsyncMessages(aMessage);
}

bool
CompositorTexturesParent::AllocShmem(size_t aSize,
                                     ipc::SharedMemory::SharedMemoryType aShmType,
                                     ipc::Shmem* aShmem)
{
  return PCompositorTexturesParent::AllocShmem(aSize, aShmType, aShmem);
}

bool
CompositorTexturesParent::AllocUnsafeShmem(size_t aSize,
                                           ipc::SharedMemory::SharedMemoryType aShmType,
                                           ipc::Shmem* aShmem)
{
  return PCompositorTexturesParent::AllocUnsafeShmem(aSize, aShmType, aShmem);
}

void
CompositorTexturesParent::DeallocShmem(ipc::Shmem& aShmem)
{
  PCompositorTexturesParent::DeallocShmem(aShmem);
}

} // namespace layers
} // namespace mozilla
