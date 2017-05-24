/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorManagerParent.h"
#include "mozilla/gfx/GPUParent.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CrossProcessCompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "VsyncSource.h"

namespace mozilla {
namespace layers {

StaticRefPtr<CompositorManagerParent> CompositorManagerParent::sInstance;
StaticMutex CompositorManagerParent::sMutex;

/* static */ already_AddRefed<CompositorManagerParent>
CompositorManagerParent::CreateSameProcess()
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());
  StaticMutexAutoLock lock(sMutex);

  // We are creating a manager for the UI process, inside the combined GPU/UI
  // process. It is created more-or-less the same but we retain a reference to
  // the parent to access state.
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initialized");
    return nullptr;
  }

  // The child is responsible for setting up the IPC channel.
  RefPtr<CompositorManagerParent> parent = new CompositorManagerParent();
  parent->SetOtherProcessId(base::GetCurrentProcId());
  sInstance = parent;
  return parent.forget();
}

/* static */ void
CompositorManagerParent::Create(Endpoint<PCompositorManagerParent>&& aEndpoint)
{
  MOZ_ASSERT(NS_IsMainThread());

  // We are creating a manager for the another process, inside the GPU process
  // (or UI process if it subsumbed the GPU process).
  MOZ_ASSERT(aEndpoint.OtherPid() != base::GetCurrentProcId());

  RefPtr<CompositorManagerParent> bridge = new CompositorManagerParent();

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

  // We rely upon the child to close the IPC channel, so this just frees our
  // reference to the parent that we kept to access state.
  RefPtr<CompositorManagerParent> parent;
  {
    StaticMutexAutoLock lock(sMutex);
    MOZ_ASSERT(sInstance);
    parent = sInstance.forget();
  }
}

/* static */ already_AddRefed<CompositorBridgeParent>
CompositorManagerParent::CreateSameProcessWidgetCompositorBridge(CSSToLayoutDeviceScale aScale,
                                                                 const CompositorOptions& aOptions,
                                                                 bool aUseExternalSurfaceSize,
                                                                 const gfx::IntSize& aSurfaceSize)
{
  MOZ_ASSERT(XRE_IsParentProcess());
  MOZ_ASSERT(NS_IsMainThread());

  StaticMutexAutoLock lock(sMutex);
  if (NS_WARN_IF(!sInstance)) {
    return nullptr;
  }

  TimeDuration vsyncRate =
    gfxPlatform::GetPlatform()->GetHardwareVsync()->GetGlobalDisplay().GetVsyncRate();

  RefPtr<CompositorBridgeParent> bridge =
    new CompositorBridgeParent(sInstance, aScale, vsyncRate, aOptions,
                               aUseExternalSurfaceSize, aSurfaceSize);
  sInstance->mPendingCompositorBridges.AppendElement(bridge);
  return bridge.forget();
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

  // Add the IPDL reference to ourself, so we can't get freed until IPDL is
  // done with us.
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

PCompositorBridgeParent*
CompositorManagerParent::AllocPCompositorBridgeParent(const CompositorBridgeOptions& aOpt)
{
  switch (aOpt.type()) {
    case CompositorBridgeOptions::TContentCompositorOptions: {
      RefPtr<CrossProcessCompositorBridgeParent> bridge =
        new CrossProcessCompositorBridgeParent(this);
      return bridge.forget().take();
    }
    case CompositorBridgeOptions::TWidgetCompositorOptions: {
      // Only the UI process is allowed to create widget compositors.
      gfx::GPUParent* gpu = gfx::GPUParent::GetSingleton();
      if (NS_WARN_IF(!gpu || OtherPid() != gpu->OtherPid())) {
        MOZ_ASSERT_UNREACHABLE("Child cannot create widget compositor!");
        break;
      }

      const WidgetCompositorOptions& opt = aOpt.get_WidgetCompositorOptions();
      RefPtr<CompositorBridgeParent> bridge =
        new CompositorBridgeParent(this, opt.scale(), opt.vsyncRate(),
                                   opt.options(), opt.useExternalSurfaceSize(),
                                   opt.surfaceSize());
      return bridge.forget().take();
    }
    case CompositorBridgeOptions::TSameProcessWidgetCompositorOptions: {
      // If the GPU and UI process are combined, we actually already created the
      // CompositorBridgeParent, so we need to reuse that to inject it into the
      // IPDL framework.
      if (NS_WARN_IF(OtherPid() != base::GetCurrentProcId())) {
        MOZ_ASSERT_UNREACHABLE("Child cannot create same process compositor!");
        break;
      }

      StaticMutexAutoLock lock(sMutex);
      MOZ_ASSERT(sInstance == this);
      MOZ_ASSERT(!mPendingCompositorBridges.IsEmpty());

      auto bridge = mPendingCompositorBridges[0].forget().take();
      mPendingCompositorBridges.RemoveElementAt(0);
      return bridge;
    }
    default:
      break;
  }

  return nullptr;
}

bool
CompositorManagerParent::DeallocPCompositorBridgeParent(PCompositorBridgeParent* aActor)
{
  RefPtr<CompositorBridgeParentBase> bridge =
    dont_AddRef(static_cast<CompositorBridgeParentBase*>(aActor));
  return true;
}

} // namespace layers
} // namespace mozilla
