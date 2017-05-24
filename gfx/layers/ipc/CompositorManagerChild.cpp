/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "mozilla/layers/CompositorManagerChild.h"
#include "mozilla/layers/CompositorManagerParent.h"

namespace mozilla {
namespace layers {

StaticRefPtr<CompositorManagerChild> CompositorManagerChild::sInstance;

/* static */ bool
CompositorManagerChild::IsInitialized()
{
  return sInstance && sInstance->mCanSend;
}

/* static */ bool
CompositorManagerChild::InitSameProcess(uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initialized");
    return false;
  }

  RefPtr<CompositorManagerParent> parent =
    CompositorManagerParent::CreateSameProcess();
  sInstance = new CompositorManagerChild(parent, aNamespace);
  return sInstance->mCanSend;
}

/* static */ bool
CompositorManagerChild::Init(Endpoint<PCompositorManagerChild>&& aEndpoint,
                             uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (sInstance) {
    MOZ_ASSERT(sInstance->mNamespace != aNamespace);
    if (NS_WARN_IF(sInstance->mCanSend)) {
      MOZ_ASSERT_UNREACHABLE("Already initialized");
      return false;
    }
  }

  sInstance = new CompositorManagerChild(Move(aEndpoint), aNamespace);
  return sInstance->mCanSend;
}

/* static */ void
CompositorManagerChild::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  CompositorBridgeChild::ShutDown();

  if (!sInstance) {
    return;
  }

  if (sInstance->mCanSend) {
    sInstance->Close();
  } else if (sInstance->OtherPid() == base::GetCurrentProcId()) {
    CompositorManagerParent::ShutdownSameProcess();
  }
  sInstance = nullptr;
}

CompositorManagerChild::CompositorManagerChild(CompositorManagerParent* aParent,
                                               uint32_t aNamespace)
  : mCanSend(false)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  MOZ_ASSERT(aParent);

  SetOtherProcessId(base::GetCurrentProcId());
  MessageLoop* loop = CompositorThreadHolder::Loop();
  ipc::MessageChannel* channel = aParent->GetIPCChannel();
  if (NS_WARN_IF(!Open(channel, loop, ipc::ChildSide))) {
    return;
  }

  mCanSend = true;
  AddRef();
}

CompositorManagerChild::CompositorManagerChild(Endpoint<PCompositorManagerChild>&& aEndpoint,
                                               uint32_t aNamespace)
  : mCanSend(false)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  if (NS_WARN_IF(!aEndpoint.Bind(this))) {
    return;
  }

  mCanSend = true;
  AddRef();
}

void
CompositorManagerChild::DeallocPCompositorManagerChild()
{
  MOZ_ASSERT(!mCanSend);
  Release();
}

void
CompositorManagerChild::ActorDestroy(ActorDestroyReason aReason)
{
  mCanSend = false;
}

} // namespace layers
} // namespace mozilla
