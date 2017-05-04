/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

nsRefPtrHashtable<nsUint64HashKey, SharedSurfaceBridgeParent> SharedSurfaceBridgeParent::sInstances;
StaticMutex SharedSurfaceBridgeParent::sMutex;

/* static */ already_AddRefed<SharedSurfaceBridgeParent>
SharedSurfaceBridgeParent::GetInstance(base::ProcessId aPid)
{
  uint64_t pid = static_cast<uint64_t>(aPid);
  RefPtr<SharedSurfaceBridgeParent> bridge;
  sInstances.Get(pid, getter_AddRefs(bridge));
  return bridge.forget();
}

/* static */ already_AddRefed<DataSourceSurface>
SharedSurfaceBridgeParent::Get(base::ProcessId aPid, const wr::ExternalImageId& aId)
{
  StaticMutexAutoLock lock(sMutex);
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());

  RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(aPid);
  if (NS_WARN_IF(!bridge)) {
    return nullptr;
  }

  RefPtr<DataSourceSurface> surface;
  bridge->mSurfaces.Get(wr::AsUint64(aId), getter_AddRefs(surface));
  return surface.forget();
}

/* static */ already_AddRefed<DataSourceSurface>
SharedSurfaceBridgeParent::GetOrWait(base::ProcessId aPid, const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  uint32_t wait = 0;

  do {
    RefPtr<DataSourceSurface> surface = Get(aPid, aId);
    if (surface) {
      printf_stderr("[AO] waited for %u messages\n", wait);
      return surface.forget();
    } else {
      mozilla::ipc::MessageChannel* channel = nullptr;

      {
        StaticMutexAutoLock lock(sMutex);
        RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(aPid);
        if (NS_WARN_IF(!bridge)) {
          return nullptr;
        }

        channel = bridge->GetIPCChannel();
        if (NS_WARN_IF(!channel)) {
          return nullptr;
        }
      }

      ++wait;
      if (!channel->WaitForIncomingMessage()) {
        return nullptr;
      }
    }
  } while(true);
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
  auto pid = base::GetCurrentProcId();
  RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(pid);
  if (NS_WARN_IF(!bridge)) {
    return;
  }

  RefPtr<SourceSurfaceSharedDataWrapper> surface =
    new SourceSurfaceSharedDataWrapper();
  surface->Init(aSurface);

  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(!bridge->mSurfaces.Contains(id));
  bridge->mSurfaces.Put(id, surface.forget());
}

/* static */ void
SharedSurfaceBridgeParent::RemoveSameProcess(const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // If the child bridge detects it is in the combined UI/GPU process, then it
  // will remove the wrapper surface directly from the hashtable.
  auto pid = base::GetCurrentProcId();
  RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(pid);
  if (NS_WARN_IF(!bridge)) {
    return;
  }

  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(bridge->mSurfaces.Contains(id));
  bridge->mSurfaces.Remove(id);
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
  auto pid = base::GetCurrentProcId();
  RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(pid);
  if (NS_WARN_IF(bridge)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }

  bridge = new SharedSurfaceBridgeParent(pid);
  uint64_t pid64 = static_cast<uint64_t>(pid);
  sInstances.Put(pid64, bridge.forget());
}

/* static */ void
SharedSurfaceBridgeParent::Create(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint)
{
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // We are creating a bridge for the content process, inside the GPU process
  // (or UI process if it subsumbed the GPU process).
  auto pid = aEndpoint.OtherPid();
  RefPtr<SharedSurfaceBridgeParent> bridge = GetInstance(pid);
  if (NS_WARN_IF(bridge)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }

  bridge = new SharedSurfaceBridgeParent(Move(aEndpoint));
  uint64_t pid64 = static_cast<uint64_t>(pid);
  sInstances.Put(pid64, bridge.forget());
}

/* static */ void
SharedSurfaceBridgeParent::ShutdownSameProcess()
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // In the case where the GPU process is the same as the UI process, we cannot
  // expect SharedSurfaceBridgeParent::DeallocPSharedSurfaceBridgeParent to be
  // called, because we did not setup the IPC hooks at all. As a result, it is
  // safe for us to free the object on the main thread rather than the IPC
  // thread.
  uint64_t pid = static_cast<uint64_t>(base::GetCurrentProcId());
  sInstances.Remove(pid);
}

SharedSurfaceBridgeParent::SharedSurfaceBridgeParent(base::ProcessId aPid)
{
  MOZ_ASSERT(NS_IsMainThread());

  // If the GPU process is the same as the UI process, this is the constructor
  // called directly to create a new bridge. While we set the process ID for
  // checking later, we don't set an endpoint and so this object lives outside
  // the IPC framework.
  SetOtherProcessId(aPid);
}

SharedSurfaceBridgeParent::SharedSurfaceBridgeParent(Endpoint<PSharedSurfaceBridgeParent>&& aEndpoint)
  : SharedSurfaceBridgeParent(aEndpoint.OtherPid())
{
  MOZ_ASSERT(NS_IsMainThread());

  // In an ideal world, since we run the bridge off the compositor thread, we
  // would be able to use async commands to share the surface, but it has been
  // determined empirically that messages can be reordered relative to the
  // protocols.
  RefPtr<Runnable> runnable = NewRunnableMethod<Endpoint<PSharedSurfaceBridgeParent>&&>(
    this,
    &SharedSurfaceBridgeParent::Bind,
    Move(aEndpoint));
  CompositorThreadHolder::Loop()->PostTask(runnable.forget());
}

SharedSurfaceBridgeParent::~SharedSurfaceBridgeParent()
{
  // Note that the destruction of a parent may not be cheap if it still has a
  // lot of surfaces still bound that require unmapping.
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
SharedSurfaceBridgeParent::DeallocPSharedSurfaceBridgeParent()
{
  {
    StaticMutexAutoLock lock(sMutex);
    uint64_t pid = static_cast<uint64_t>(OtherPid());
    sInstances.Remove(pid);
  }

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
                                aDesc.format(), aDesc.handle()))) {
    return IPC_OK();
  }

  StaticMutexAutoLock lock(sMutex);
  uint64_t id = wr::AsUint64(aId);
  MOZ_ASSERT(!mSurfaces.Contains(id));
  mSurfaces.Put(id, surface.forget());
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  MOZ_ASSERT(OtherPid() != base::GetCurrentProcId());

  StaticMutexAutoLock lock(sMutex);
  uint64_t id = wr::AsUint64(aId);
  mSurfaces.Remove(id);
  return IPC_OK();
}

} // namespace layers
} // namespace mozilla
