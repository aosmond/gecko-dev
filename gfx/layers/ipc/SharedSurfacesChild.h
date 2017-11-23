/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACESCHILD_H
#define MOZILLA_GFX_SHAREDSURFACESCHILD_H

#include <stddef.h>                     // for size_t
#include <stdint.h>                     // for uint32_t, uint64_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/Maybe.h"              // for Maybe
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/gfx/UserData.h"       // for UserDataKey
#include "mozilla/webrender/WebRenderTypes.h" // for wr::ImageKey

namespace mozilla {
namespace gfx {
class SourceSurface;
class SourceSurfaceRecording;
class SourceSurfaceSharedData;
} // namespace gfx

namespace wr {
class IpcResourceUpdateQueue;
} // namespace wr

namespace layers {

class CompositorManagerChild;
class WebRenderLayerManager;

class SharedSurfacesChild final
{
public:
  static nsresult Share(gfx::SourceSurface* aSurface,
                        WebRenderLayerManager* aManager,
                        wr::IpcResourceUpdateQueue& aResources,
                        wr::ImageKey& aKey);

  static nsresult Share(ImageContainer* aContainer,
                        WebRenderLayerManager* aManager,
                        wr::IpcResourceUpdateQueue& aResources,
                        wr::ImageKey& aKey);

  static void SurfaceUpdated(gfx::SourceSurface* aSurface);

private:
  static nsresult Share(gfx::SourceSurfaceSharedData* aSurface,
                        WebRenderLayerManager* aManager,
                        wr::IpcResourceUpdateQueue& aResources,
                        wr::ImageKey& aKey);

  static nsresult Share(gfx::SourceSurfaceRecording* aSurface,
                        WebRenderLayerManager* aManager,
                        wr::IpcResourceUpdateQueue& aResources,
                        wr::ImageKey& aKey);

  SharedSurfacesChild() = delete;
  ~SharedSurfacesChild() = delete;

  class ImageKeyData;
  class UserData;
  class RecordUserData;
  class SharedUserData;

  static UserDataKey sSharedKey;

  static void Unshare(const Maybe<wr::ExternalImageId>& aId,
                      nsTArray<ImageKeyData>& aKeys);
  static void DestroySharedUserData(void* aClosure);
  static void DestroyRecordUserData(void* aClosure);
};

} // namespace layers
} // namespace mozilla

#endif
