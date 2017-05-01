/* vim: set ts=2 sw=2 et tw=80: */
/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeParent.h"

namespace mozilla {
namespace layers {

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
  return IPC_OK();
}

mozilla::ipc::IPCResult
SharedSurfaceBridgeParent::RecvRemove(const uint64_t& aId)
{
  return IPC_OK();
}

} // namespace layers
} // namespace mozilla
