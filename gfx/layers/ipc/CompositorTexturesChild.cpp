/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorTexturesChild.h"
#include "mozilla/layers/TextureClient.h"

namespace mozilla {
namespace layers {

CompositorTexturesChild::CompositorTexturesChild()
  : mMessageLoop(MessageLoop::current())
  , mSectionAllocator(nullptr)
  , mCanSend(true)
{ }

CompositorTexturesChild::~CompositorTexturesChild()
{ }

void
CompositorTexturesChild::ActorDestroy(ActorDestroyReason aReason)
{
  mCanSend = false;
}

ipc::IPCResult
CompositorTexturesChild::RecvParentAsyncMessages(InfallibleTArray<AsyncParentMessageData>&& aMessages)
{
  return IPC_OK();
}

PTextureChild*
CompositorTexturesChild::AllocPTextureChild(const SurfaceDescriptor& aSharedData,
                                            const LayersBackend& aLayersBackend,
                                            const TextureFlags& aFlags,
                                            const uint64_t& aId,
                                            const uint64_t& aSerial,
                                            const wr::MaybeExternalImageId& aExternalImageId)
{
  return TextureClient::CreateIPDLActor();
}

bool
CompositorTexturesChild::DeallocPTextureChild(PTextureChild* actor)
{
  return TextureClient::DestroyIPDLActor(actor);
}

bool
CompositorTexturesChild::AllocShmem(size_t aSize,
                                    ipc::SharedMemory::SharedMemoryType aShmType,
                                    ipc::Shmem* aShmem)
{
  return PCompositorTexturesChild::AllocShmem(aSize, aShmType, aShmem);
}

bool CompositorTexturesChild::AllocUnsafeShmem(size_t aSize,
                        ipc::SharedMemory::SharedMemoryType aShmType,
                        ipc::Shmem* aShmem)
{
  return PCompositorTexturesChild::AllocUnsafeShmem(aSize, aShmType, aShmem);
}

bool CompositorTexturesChild::DeallocShmem(ipc::Shmem& aShmem)
{
  return PCompositorTexturesChild::DeallocShmem(aShmem);
}

bool CompositorTexturesChild::IsSameProcess() const
{
  return OtherPid() == base::GetCurrentProcId();
}

base::ProcessId
CompositorTexturesChild::GetParentPid() const
{
  return OtherPid();
}

MessageLoop*
CompositorTexturesChild::GetMessageLoop() const
{
  return mMessageLoop;
}

FixedSizeSmallShmemSectionAllocator*
CompositorTexturesChild::GetTileLockAllocator()
{
  return mSectionAllocator;
}

void
CompositorTexturesChild::CancelWaitForRecycle(uint64_t aTextureId)
{
}

wr::MaybeExternalImageId
CompositorTexturesChild::GetNextExternalImageId()
{
  return Nothing();
}

PTextureChild*
CompositorTexturesChild::CreateTexture(const SurfaceDescriptor& aSharedData,
                                       LayersBackend aLayersBackend,
                                       TextureFlags aFlags,
                                       uint64_t aSerial,
                                       wr::MaybeExternalImageId& aExternalImageId,
                                       nsIEventTarget* aTarget)
{
  return nullptr;
}

} // namespace layers
} // namespace mozilla
