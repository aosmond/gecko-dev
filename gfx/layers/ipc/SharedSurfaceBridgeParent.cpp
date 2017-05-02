/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeParent.h"
#include "mozilla/StaticPtr.h"                // for StaticRefPtr

namespace mozilla {
namespace layers {

StaticRefPtr<SharedSurfaceBridgeParent> SharedSurfaceBridgeParent::sInstance;

/* static */ already_AddRefed<DataSourceSurface>
SharedSurfaceBridgeParent::Get(uint64_t aId)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!sInstance) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> surface;
  sInstance->mSurfaces.Get(aId, getter_AddRefs(surface));
  return surface.forget();
}

/* static */ void
SharedSurfaceBridgeParent::Insert(uint64_t aId,
                                  already_AddRefed<DataSourceSurface> aSurface)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  if (!sInstance) {
    return;
  }

  RefPtr<DataSourceSurface> surface = aSurface;
  MOZ_ASSERT(!sInstance->mSurfaces.Contains(aId));
  sInstance->mSurfaces.Put(aId, surface.forget());
}

/* static */ void
SharedSurfaceBridgeParent::Remove(uint64_t aId)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  if (!sInstance) {
    return;
  }

  MOZ_ASSERT(sInstance->mSurfaces.Contains(aId));
  sInstance->mSurfaces.Remove(aId);
}

/* static */ void
SharedSurfaceBridgeParent::Init()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }
  sInstance = new SharedSurfaceBridgeParent();
}

/* static */ void
SharedSurfaceBridgeParent::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  sInstance = nullptr;
}

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
  MOZ_ASSERT(NS_IsMainThread());

  RefPtr<SourceSurfaceSharedDataWrapper> surface = new SourceSurfaceSharedDataWrapper();
  if (NS_WARN_IF(!surface->Init(aSize, aStride, aFormat, aHandle))) {
    return IPC_OK();
  }

  MOZ_ASSERT(!mSurfaces.Contains(aId));
  mSurfaces.Put(aId, surface.forget());
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const uint64_t& aId)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(mSurfaces.Contains(aId));
  mSurfaces.Remove(aId);
  return IPC_OK();
}

void
SharedSurfaceBridgeParent::ActorDestroy(ActorDestroyReason aReason)
{
}

} // namespace layers
} // namespace mozilla
