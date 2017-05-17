/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorManagerParent.h"
#include "mozilla/layers/CompositorThread.h"

namespace mozilla {
namespace layers {

StaticRefPtr<CompositorManagerParent> CompositorManagerParent::sInstance;
StaticMutex CompositorManagerParent::sMutex;

/* static */ void
CompositorManagerParent::CreateSameProcess()
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

  sInstance = new CompositorManagerParent();

  // If the GPU process is the same as the UI process, this is the constructor
  // called directly to create a new bridge. While we set the process ID for
  // checking later, we don't set an endpoint and so this object lives outside
  // the IPC framework.
  sInstance->SetOtherProcessId(base::GetCurrentProcId());
}

/* static */ void
CompositorManagerParent::Create(Endpoint<PCompositorManagerParent>&& aEndpoint)
{
  MOZ_ASSERT(NS_IsMainThread());

  // We are creating a bridge for the content process, inside the GPU process
  // (or UI process if it subsumbed the GPU process).
  MOZ_ASSERT(aEndpoint.OtherPid() != base::GetCurrentProcId());

  RefPtr<CompositorManagerParent> bridge = new CompositorManagerParent();

  // In an ideal world, since we run the bridge off the compositor thread, we
  // would be able to use async commands to share the surface, but they don't
  // have the same protocol manager, and thus have different process links.
  RefPtr<Runnable> runnable = NewRunnableMethod<Endpoint<PCompositorManagerParent>&&>(
    bridge,
    &CompositorManagerParent::Bind,
    Move(aEndpoint));
  CompositorThreadHolder::Loop()->PostTask(runnable.forget());
}

/* static */ void
CompositorManagerParent::ShutdownSameProcess()
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  // In the case where the GPU process is the same as the UI process, we cannot
  // expect CompositorManagerParent::DeallocPCompositorManagerParent to be
  // called, because we did not setup the IPC hooks at all. As a result, it is
  // safe for us to free the object on the main thread rather than the IPC
  // thread.
  RefPtr<CompositorManagerParent> parent;
  {
    StaticMutexAutoLock lock(sMutex);
    MOZ_ASSERT(sInstance);
    parent = sInstance.forget();
  }
}

CompositorManagerParent::CompositorManagerParent()
  : mCompositorThreadHolder(CompositorThreadHolder::GetSingleton())
{ }

CompositorManagerParent::~CompositorManagerParent()
{ }

void
CompositorManagerParent::Bind(Endpoint<PCompositorManagerParent>&& aEndpoint)
{
  MOZ_ASSERT(CompositorThreadHolder::IsInCompositorThread());
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return;
  }

  // Add the IPDL reference to ourself, so even if we get removed from the
  // instance table, we won't get freed until IPDL is done with us.
  AddRef();
}

void
CompositorManagerParent::ActorDestroy(ActorDestroyReason aReason)
{ }

void
CompositorManagerParent::DeallocPCompositorManagerParent()
{
  Release();
}

ipc::IPCResult
CompositorManagerParent::RecvFoo()
{
  return IPC_OK();
}

#if 0
mozilla::ipc::IPCResult
GPUParent::RecvNewWidgetCompositor(Endpoint<layers::PCompositorBridgeParent>&& aEndpoint,
                                   const CSSToLayoutDeviceScale& aScale,
                                   const TimeDuration& aVsyncRate,
                                   const CompositorOptions& aOptions,
                                   const bool& aUseExternalSurfaceSize,
                                   const IntSize& aSurfaceSize)
{
  RefPtr<CompositorBridgeParent> cbp =
    new CompositorBridgeParent(aScale, aVsyncRate, aOptions, aUseExternalSurfaceSize, aSurfaceSize);

  MessageLoop* loop = CompositorThreadHolder::Loop();
  loop->PostTask(NewRunnableFunction(OpenParent, cbp, Move(aEndpoint)));
  return IPC_OK();
}
#endif

} // namespace layers
} // namespace mozilla
