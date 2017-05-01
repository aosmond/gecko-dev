/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACEBRIDGECHILD_H
#define MOZILLA_GFX_SHAREDSURFACEBRIDGECHILD_H

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint64_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/layers/PSharedSurfaceBridgeChild.h"

namespace mozilla {
namespace gfx {
class SourceSurfaceSharedData; 
} // namespace gfx

namespace layers {

class SharedSurfaceBridgeChild : public PSharedSurfaceBridgeChild
{
  NS_INLINE_DECL_REFCOUNTING(SharedSurfaceBridgeChild)

public:
  SharedSurfaceBridgeChild(uint32_t aNamespace);

  nsresult Share(SourceSurfaceSharedData* aSurface, uint64_t& aId);
  bool IPCOpen() const { return true; } // FIXME

private:
  ~SharedSurfaceBridgeChild() override
  { }

  uint64_t GetNextId();

  uint32_t mNamespace;
  uint32_t mResourceId;
};

} // namespace layers
} // namespace mozilla

#endif
