/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeParent.h"
#include "mozilla/StaticPtr.h"
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace layers {

class SharedSurfaceManagerImpl final
{
public:
  SharedSurfaceManagerImpl()
  { }

  already_AddRefed<DataSourceSurface> Get(uint64_t aId)
  {
    MOZ_ASSERT(NS_IsMainThread());
    RefPtr<DataSourceSurface> surface;
    mSurfaces.Get(aId, getter_AddRefs(surface));
    return surface.forget();
  }

  nsresult ShareToParent(SourceSurfaceSharedData* aSurface, uint64_t& aId)
  {
    MOZ_ASSERT(NS_IsMainThread());
  }

  void Add(uint64_t aId, already_AddRefed<DataSourceSurface> aSurface)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(!mSurfaces.Contains(aId));
    RefPtr<DataSourceSurface> surf = aSurface;
    mSurfaces.Put(aId, surf.forget());
  }

  void Remove(uint64_t aId)
  {
    MOZ_ASSERT(NS_IsMainThread());
    MOZ_ASSERT(mSurfaces.Contains(aId));
    mSurfaces.Remove(aId);
  }

private:
  ~SharedSurfaceManagerImpl()
  { }

  nsRefPtrHashtable<nsUint64HashKey, DataSourceSurface> mSurfaces;
};

static StaticRefPtr<SharedSurfaceManagerImpl> sManager;

SharedSurfaceBridgeParent::SharedSurfaceBridgeParent()
{
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvAdd(const uint64_t& aId,
                                   const gfx::IntSize& aSize,
                                   const int32_t& aStride,
                                   const gfx::SurfaceFormat& aFormat,
                                   const mozilla::ipc::SharedMemoryBasic::Handle& aHandle)
{
  RefPtr<SourceSurfaceSharedDataWrapper> surface = new SourceSurfaceSharedDataWrapper();
  if (NS_WARN_IF(!surface->Init(aSize, aStride, aFormat, aHandle))) {
    return IPC_OK();
  }

  if (sManager) {
    sManager->Add(aId, surface.forget());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const uint64_t& aId)
{
  if (sManager) {
    sManager->Remove(aId);
  }
  return IPC_OK();
}

} // namespace layers
} // namespace mozilla
