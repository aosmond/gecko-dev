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

namespace mozilla {
namespace layers {

class CompositorTexturesChild final : public PCompositorTexturesChild
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorTexturesChild)

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

private:
  ~CompositorTexturesChild() override;
};

} // namespace layers
} // namespace mozilla

#endif
