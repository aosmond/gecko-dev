/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorTexturesChild.h"

namespace mozilla {
namespace layers {

CompositorTexturesChild::CompositorTexturesChild()
{ }

CompositorTexturesChild::~CompositorTexturesChild()
{ }

void
CompositorTexturesChild::ActorDestroy(ActorDestroyReason aReason)
{ }

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
  return nullptr;
}

bool
CompositorTexturesChild::DeallocPTextureChild(PTextureChild* actor)
{
  return true;
}

} // namespace layers
} // namespace mozilla
