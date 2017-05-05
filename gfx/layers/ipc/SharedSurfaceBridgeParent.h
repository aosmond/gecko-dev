/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACEBRIDGEPARENT_H
#define MOZILLA_GFX_SHAREDSURFACEBRIDGEPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/StaticMutex.h"        // for StaticMutex
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/gfx/2D.h"             // for SurfaceFormat
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/layers/PSharedSurfaceBridgeParent.h"
#include "mozilla/webrender/WebRenderTypes.h" // for wr::ExternalImageId
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace gfx {
class DataSourceSurface;
class SourceSurfaceSharedData;
class SourceSurfaceSharedDataWrapper;
} // namespace gfx

namespace layers {

class SharedSurfaceBridgeChild;
class CompositorThreadHolder;

class SharedSurfaceBridgeParent final : public PSharedSurfaceBridgeParent
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(SharedSurfaceBridgeParent)

public:
  static void CreateSameProcess();
  static void Create(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint);
  static void ShutdownSameProcess();
  static already_AddRefed<gfx::DataSourceSurface> Get(const wr::ExternalImageId& aId);

  mozilla::ipc::IPCResult RecvAdd(const wr::ExternalImageId& aId,
                                  const SurfaceDescriptorShared& aDesc) override;

  mozilla::ipc::IPCResult RecvRemove(const wr::ExternalImageId& aId) override;

  mozilla::ipc::IPCResult RecvFlush() override;

  void ActorDestroy(ActorDestroyReason aReason) override;

  void OnChannelConnected(int32_t pid) override;

private:
  friend class SharedSurfaceBridgeChild;

  static void AddSameProcess(const wr::ExternalImageId& aId,
                             gfx::SourceSurfaceSharedData* aSurface);
  static void RemoveSameProcess(const wr::ExternalImageId& aId);

  static nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurfaceSharedDataWrapper> sSurfaces;
  static StaticRefPtr<SharedSurfaceBridgeParent> sInstance;
  static StaticMutex sMutex;

  SharedSurfaceBridgeParent();
  void Bind(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint);

  ~SharedSurfaceBridgeParent() override;

  void DeallocPSharedSurfaceBridgeParent() override;

  RefPtr<CompositorThreadHolder> mCompositorThreadHolder;
};

} // namespace layers
} // namespace mozilla

#endif
