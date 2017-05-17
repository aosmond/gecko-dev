/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "CompositorManagerChild.h"
#include "CompositorManagerParent.h"

namespace mozilla {
namespace layers {

StaticRefPtr<CompositorManagerChild> CompositorManagerChild::sInstance;

/* static */ bool
CompositorManagerChild::IsInitialized()
{
  return !!sInstance;
}

/* static */ void
CompositorManagerChild::InitSameProcess(uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }
  CompositorManagerParent::CreateSameProcess();
  sInstance = new CompositorManagerChild(aNamespace);
}

/* static */ bool
CompositorManagerChild::Init(Endpoint<PCompositorManagerChild>&& aEndpoint,
                             uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return false;
  }
  sInstance = new CompositorManagerChild(Move(aEndpoint), aNamespace);
  return true;
}

/* static */ void
CompositorManagerChild::Reinit(Endpoint<PCompositorManagerChild>&& aEndpoint,
                               uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  MOZ_ASSERT(sInstance->mNamespace != aNamespace);
  sInstance = new CompositorManagerChild(Move(aEndpoint), aNamespace);
  return true;
}

/* static */ void
CompositorManagerChild::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  if (!sInstance) {
    return;
  }

  if (!sInstance->mClosed) {
    sInstance->Close();
  } else if (sInstance->OtherPid() == base::GetCurrentProcId()) {
    CompositorManagerParent::ShutdownSameProcess();
  }
  sInstance = nullptr;
}

/* static */ bool
CompositorManagerChild::CreateCompositorBridge(uint32_t aNamespace)
{
  return false;
}

CompositorManagerChild::CompositorManagerChild(uint32_t aNamespace)
  : mClosed(true)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  SetOtherProcessId(base::GetCurrentProcId());
}

CompositorManagerChild::CompositorManagerChild(Endpoint<PCompositorManagerChild>&& aEndpoint,
                                               uint32_t aNamespace)
  : mClosed(true)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return;
  }

  mClosed = false;
  AddRef();
}

void
CompositorManagerChild::DeallocPCompositorManagerChild()
{
  MOZ_ASSERT(mClosed);
  Release();
}

void
CompositorManagerChild::ActorDestroy(ActorDestroyReason aReason)
{
  mClosed = true;
}

} // namespace layers
} // namespace mozilla
