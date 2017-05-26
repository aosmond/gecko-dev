/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorTexturesParent.h"

namespace mozilla {
namespace layers {

CompositorTexturesParent::CompositorTexturesParent()
{ }

CompositorTexturesParent::~CompositorTexturesParent()
{ }

void
CompositorTexturesParent::ActorDestroy(ActorDestroyReason aReason)
{ }

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
  return nullptr;
}

bool
CompositorTexturesParent::DeallocPTextureParent(PTextureParent* actor)
{
  return true;
}

} // namespace layers
} // namespace mozilla
