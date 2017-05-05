/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeParent.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurfaceSharedDataWrapper> SharedSurfaceBridgeParent::sSurfaces;
StaticRefPtr<SharedSurfaceBridgeParent> SharedSurfaceBridgeParent::sInstance;
StaticMutex SharedSurfaceBridgeParent::sMutex;

/* static */ already_AddRefed<DataSourceSurface>
SharedSurfaceBridgeParent::Get(const wr::ExternalImageId& aId)
{
  StaticMutexAutoLock lock(sMutex);

  RefPtr<SourceSurfaceSharedDataWrapper> surface;
  sSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface));
  return surface.forget();
}

/* static */ void
SharedSurfaceBridgeParent::AddSameProcess(const wr::ExternalImageId& aId,
                                          SourceSurfaceSharedData* aSurface)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // If the child bridge detects it is in the combined UI/GPU process, then it
  // will insert a wrapper surface holding the shared memory buffer directly.
  // This is good because we avoid mapping the same shared memory twice, but
  // still allow the original surface to be freed and remove the wrapper from
  // the table when it is no longer needed.
  RefPtr<SourceSurfaceSharedDataWrapper> surface =
    new SourceSurfaceSharedDataWrapper();
  surface->Init(aSurface);

  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(!sSurfaces.Contains(id));
  sSurfaces.Put(id, surface.forget());
}

/* static */ void
SharedSurfaceBridgeParent::RemoveSameProcess(const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // If the child bridge detects it is in the combined UI/GPU process, then it
  // will remove the wrapper surface directly from the hashtable.
  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(sSurfaces.Contains(id));
  sSurfaces.Remove(id);
}

/* static */ void
SharedSurfaceBridgeParent::CreateSameProcess()
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // We are creating a bridge for the UI process, inside the combined GPU/UI
  // process. This is because the UI process itself may have decoded surfaces
  // it wants to render, but the other parts of the code are unaware of the
  // process distinction. This method of creation does not setup any IPC hooks.
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }

  sInstance = new SharedSurfaceBridgeParent();

  // If the GPU process is the same as the UI process, this is the constructor
  // called directly to create a new bridge. While we set the process ID for
  // checking later, we don't set an endpoint and so this object lives outside
  // the IPC framework.
  sInstance->SetOtherProcessId(base::GetCurrentProcId());
}

/* static */ void
SharedSurfaceBridgeParent::Create(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint)
{
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // We are creating a bridge for the content process, inside the GPU process
  // (or UI process if it subsumbed the GPU process).
  MOZ_ASSERT(aEndpoint.OtherPid() != base::GetCurrentProcId());

  RefPtr<SharedSurfaceBridgeParent> bridge = new SharedSurfaceBridgeParent();

  // In an ideal world, since we run the bridge off the compositor thread, we
  // would be able to use async commands to share the surface, but they don't
  // have the same protocol manager, and thus have different process links.
  RefPtr<Runnable> runnable = NewRunnableMethod<Endpoint<PSharedSurfaceBridgeParent>&&>(
    bridge,
    &SharedSurfaceBridgeParent::Bind,
    Move(aEndpoint));
  CompositorThreadHolder::Loop()->PostTask(runnable.forget());
}

/* static */ void
SharedSurfaceBridgeParent::ShutdownSameProcess()
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  // In the case where the GPU process is the same as the UI process, we cannot
  // expect SharedSurfaceBridgeParent::DeallocPSharedSurfaceBridgeParent to be
  // called, because we did not setup the IPC hooks at all. As a result, it is
  // safe for us to free the object on the main thread rather than the IPC
  // thread.
  RefPtr<SharedSurfaceBridgeParent> parent;
  {
    StaticMutexAutoLock lock(sMutex);
    MOZ_ASSERT(sInstance);
    parent = sInstance.forget();
  }
}

SharedSurfaceBridgeParent::SharedSurfaceBridgeParent()
{ }

SharedSurfaceBridgeParent::~SharedSurfaceBridgeParent()
{
  // Note that the destruction of a parent may not be cheap if it still has a
  // lot of surfaces still bound that require unmapping.
  StaticMutexAutoLock lock(sMutex);
  auto pid = OtherPid();
  for (auto i = sSurfaces.Iter(); !i.Done(); i.Next()) {
    if (i.Data()->GetCreatorPid() == pid) {
      i.Remove();
    }
  }
}

void
SharedSurfaceBridgeParent::Bind(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (aEndpoint.Bind(this)) {
    // Add the IPDL reference to ourself, so even if we get removed from the
    // instance table, we won't get freed until IPDL is done with us.
    AddRef();
  }
}

void
SharedSurfaceBridgeParent::OnChannelConnected(int32_t aPid)
{
  mCompositorThreadHolder = CompositorThreadHolder::GetSingleton();
}

void
SharedSurfaceBridgeParent::DeallocPSharedSurfaceBridgeParent()
{
  Release();
}

void
SharedSurfaceBridgeParent::ActorDestroy(ActorDestroyReason aReason)
{
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvAdd(const wr::ExternalImageId& aId,
                                   const SurfaceDescriptorShared& aDesc)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(OtherPid() != base::GetCurrentProcId());

  // Note that the surface wrapper maps in the given handle as read only.
  RefPtr<SourceSurfaceSharedDataWrapper> surface =
    new SourceSurfaceSharedDataWrapper();
  if (NS_WARN_IF(!surface->Init(aDesc.size(), aDesc.stride(),
                                aDesc.format(), aDesc.handle(),
                                OtherPid()))) {
    return IPC_OK();
  }

  StaticMutexAutoLock lock(sMutex);
  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(!sSurfaces.Contains(id));
  sSurfaces.Put(id, surface.forget());
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(OtherPid() != base::GetCurrentProcId());

  StaticMutexAutoLock lock(sMutex);
  uint64_t id = wr::AsUint64(aId);
  sSurfaces.Remove(id);
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvFlush()
{
  return IPC_OK();
}

} // namespace layers
} // namespace mozilla
