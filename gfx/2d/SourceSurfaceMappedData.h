/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACEMAPPEDDATA_H_
#define MOZILLA_GFX_SOURCESURFACEMAPPEDDATA_H_

#include "mozilla/gfx/2D.h"

namespace mozilla {
namespace gfx {

/**
 * This class is used to wrap another surface to force it to remain mapped for
 * the duration of the parent's lifetime. This is useful to wrap surfaces such
 * as SourceSurfaceVolatileData, to allow the entity providing the surface to
 * ensure the it has not been unmapped before passing it around.
 */
class SourceSurfaceMappedData final : public DataSourceSurface
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceMappedData, override)

  explicit SourceSurfaceMappedData(DataSourceSurface* aChild, MapType aType)
    : mScopedMap(aChild, aType)
    , mSize(aChild->GetSize())
    , mType(aType)
    , mFormat(aChild->GetFormat())
    , mOnHeap(aChild->OnHeap())
  {
  }

  uint8_t *GetData() override { return mScopedMap.GetData(); }
  int32_t Stride() override { return mScopedMap.GetStride(); }

  SurfaceType GetType() const override { return SurfaceType::DATA; }
  IntSize GetSize() const override { return mSize; }
  SurfaceFormat GetFormat() const override { return mFormat; }

  bool OnHeap() const override { return mOnHeap; }

  bool Map(MapType aType, MappedSurface *aMappedSurface) override
  {
    if (mType == MapType::READ_WRITE || mType == aType) {
      *aMappedSurface = *mScopedMap.GetMappedSurface();
      mIsMapped = mScopedMap.IsMapped();
      return mIsMapped;
    }

    MOZ_ASSERT_UNREACHABLE("Mismatched mapped expectations!");
    return false;
  }

private:
  ~SourceSurfaceMappedData() override
  {
  }

  ScopedMap mScopedMap;
  IntSize mSize;
  MapType mType;
  SurfaceFormat mFormat;
  bool mOnHeap;
};

} // namespace gfx
} // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACEMAPPEDDATA_H_ */
