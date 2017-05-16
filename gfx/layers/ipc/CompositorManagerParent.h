/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_COMPOSITORMANAGERPARENT_H
#define MOZILLA_GFX_COMPOSITORMANAGERPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/StaticMutex.h"        // for StaticMutex
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/layers/PCompositorManagerParent.h"

namespace mozilla {
namespace layers {

class CompositorThreadHolder;

class CompositorManagerParent final : public PCompositorManagerParent
{
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(CompositorManagerParent)

public:
  static void CreateSameProcess();
  static void Create(Endpoint<PCompositorManagerParent>&& aEndpoint);
  static void ShutdownSameProcess();

  ipc::IPCResult RecvFoo() override;
  void ActorDestroy(ActorDestroyReason aReason) override;

private:
  static StaticRefPtr<CompositorManagerParent> sInstance;
  static StaticMutex sMutex;

  CompositorManagerParent();
  void Bind(Endpoint<PCompositorManagerParent>&& aEndpoint);

  ~CompositorManagerParent() override;

  void DeallocPCompositorManagerParent() override;

  RefPtr<CompositorThreadHolder> mCompositorThreadHolder;
};

} // namespace layers
} // namespace mozilla

#endif
