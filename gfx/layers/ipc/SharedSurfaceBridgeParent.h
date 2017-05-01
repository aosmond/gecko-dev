/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACEBRIDGEPARENT_H
#define MOZILLA_GFX_SHAREDSURFACEBRIDGEPARENT_H

#include <stdint.h>                     // for uint32_t, uint64_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/gfx/2D.h"             // for SurfaceFormat
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/layers/PSharedSurfaceBridgeParent.h"

namespace mozilla {
namespace gfx {
class SourceSurfaceSharedData;
} // namespace gfx

namespace layers {

class SharedSurfaceManager final
{
public:
  static already_AddRefed<DataSourceSurface> Get(uint64_t aId);
  static nsresult ShareToParent(SourceSurfaceSharedData* aSurface, uint64_t& aId);
private:
  SharedSurfaceManager() = delete;
  ~SharedSurfaceManager() = delete;
};

class SharedSurfaceBridgeParent final : public PSharedSurfaceBridgeParent
{
public:
  SharedSurfaceBridgeParent();

  mozilla::ipc::IPCResult RecvAdd(const uint64_t& aId,
                                  const gfx::IntSize& aSize,
                                  const int32_t& aStride,
                                  const gfx::SurfaceFormat& aFormat,
                                  const mozilla::ipc::SharedMemoryBasic::Handle& aHandle) override;

  mozilla::ipc::IPCResult RecvRemove(const uint64_t& aId) override;
};

} // namespace layers
} // namespace mozilla

#endif
