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

class SharedSurfaceManagerImpl final : public nsIObserver
{
  NS_DECL_ISUPPORTS

public:
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

  nsresult Observe(nsISupports* aSubject,
                   const char* aTopic,
                   const char16_t* aData) override
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (!nsCRT::strcmp(aTopic, NS_XPCOM_SHUTDOWN_OBSERVER_ID)) {
      sInstance = nullptr;
      sShutdown = true;
    }
    return NS_OK;
  }

  static SharedSurfaceManagerImpl* GetInstance()
  {
    MOZ_ASSERT(NS_IsMainThread());
    if (MOZ_UNLIKELY(!sInstance && !sShutdown)) {
      sInstance = new SharedSurfaceManagerImpl();
      nsCOMPtr<nsIObserverService> service =
        mozilla::services::GetObserverService();
      if (service) {
        service->AddObserver(sInstance, NS_XPCOM_SHUTDOWN_OBSERVER_ID, true);
      }
    }
    return sInstance;
  }

private:
  SharedSurfaceManagerImpl()
  { }

  ~SharedSurfaceManagerImpl()
  {
    nsCOMPtr<nsIObserverService> service =
      mozilla::services::GetObserverService();
    if (service) {
      service->RemoveObserver(this, NS_XPCOM_SHUTDOWN_OBSERVER_ID);
    }
  }

  nsRefPtrHashtable<nsUint64HashKey, DataSourceSurface> mSurfaces;
  static StaticRefPtr<SharedSurfaceManagerImpl> sInstance;
  static bool sShutdown;
};

StaticRefPtr<SharedSurfaceManagerImpl> SharedSurfaceManagerImpl::sInstance;
bool SharedSurfaceManagerImpl::sShutdown = false;

NS_IMPL_ISUPPORTS(SharedSurfaceManagerImpl, nsIObserver);

/* static */ already_AddRefed<DataSourceSurface>
SharedSurfaceManager::Get(uint64_t aId)
{
  auto mgr = SharedSurfaceManagerImpl::GetInstance();
  if (!mgr) {
    return nullptr;
  }
  return mgr->Get(aId);
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
  RefPtr<SourceSurfaceSharedDataWrapper> surface = new SourceSurfaceSharedDataWrapper();
  if (NS_WARN_IF(!surface->Init(aSize, aStride, aFormat, aHandle))) {
    return IPC_OK();
  }

  auto mgr = SharedSurfaceManagerImpl::GetInstance();
  if (mgr) {
    mgr->Add(aId, surface.forget());
  }
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const uint64_t& aId)
{
  auto mgr = SharedSurfaceManagerImpl::GetInstance();
  if (mgr) {
    mgr->Remove(aId);
  }
  return IPC_OK();
}

} // namespace layers
} // namespace mozilla
