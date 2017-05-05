/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACEBRIDGECHILD_H
#define MOZILLA_GFX_SHAREDSURFACEBRIDGECHILD_H

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint64_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/layers/PSharedSurfaceBridgeChild.h"
#include "mozilla/webrender/WebRenderTypes.h" // for wr::ExternalImageId

namespace mozilla {
namespace gfx {
class SourceSurfaceSharedData;
} // namespace gfx

namespace layers {

class SharedSurfaceBridgeChild : public PSharedSurfaceBridgeChild
{
  NS_INLINE_DECL_REFCOUNTING(SharedSurfaceBridgeChild)

public:
  static bool IsInitialized();
  static void InitSameProcess(uint32_t aNamespace);
  static void Init(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                   uint32_t aNamespace);
  static void Reinit(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                     uint32_t aNamespace);
  static void Shutdown();
  static nsresult Share(gfx::SourceSurfaceSharedData* aSurface, wr::ExternalImageId& aId);
  static void Flush();
  void ActorDestroy(ActorDestroyReason aReason) override;

private:
  class SharedUserData;

  static void Unshare(const wr::ExternalImageId& aId);
  static void DestroySharedUserData(void* aClosure);
  static StaticRefPtr<SharedSurfaceBridgeChild> sInstance;

  explicit SharedSurfaceBridgeChild(uint32_t aNamespace);

  SharedSurfaceBridgeChild(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                           uint32_t aNamespace);

  ~SharedSurfaceBridgeChild() override
  { }

  void DeallocPSharedSurfaceBridgeChild() override;

  nsresult ShareInternal(gfx::SourceSurfaceSharedData* aSurface, wr::ExternalImageId& aId);
  void UnshareInternal(const wr::ExternalImageId& aId);
  void FlushInternal();

  bool OwnsId(const wr::ExternalImageId& aId) const;
  wr::ExternalImageId GetNextId();

  bool mClosed;
  uint32_t mNamespace;
  uint32_t mResourceId;
  uint32_t mOutstandingAdds;
};

} // namespace layers
} // namespace mozilla

#endif
