/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACESPARENT_H
#define MOZILLA_GFX_SHAREDSURFACESPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "mozilla/StaticMutex.h"        // for StaticMutex
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/gfx/2D.h"             // for SurfaceFormat
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/webrender/WebRenderTypes.h" // for wr::ExternalImageId
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace gfx {
class DataSourceSurface;
class SourceSurfaceSharedData;
class SourceSurfaceSharedDataWrapper;
} // namespace gfx

namespace layers {

class SharedSurfacesChild;

class SharedSurfacesParent final
{
public:
  static already_AddRefed<gfx::DataSourceSurface> Get(const wr::ExternalImageId& aId);

  static void Add(const wr::ExternalImageId& aId,
                  const SurfaceDescriptorShared& aDesc,
                  base::ProcessId aPid);

  static void Remove(const wr::ExternalImageId& aId);

  static void DestroyProcess(base::ProcessId aPid);

private:
  friend class SharedSurfacesChild;

  static void AddSameProcess(const wr::ExternalImageId& aId,
                             gfx::SourceSurfaceSharedData* aSurface);
  static void RemoveSameProcess(const wr::ExternalImageId& aId);

  static nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurfaceSharedDataWrapper> sSurfaces;
  static StaticMutex sMutex;

  SharedSurfacesParent() = delete;
  ~SharedSurfacesParent() = delete;
};

} // namespace layers
} // namespace mozilla

#endif
