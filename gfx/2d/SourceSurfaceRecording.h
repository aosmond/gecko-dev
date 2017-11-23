/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SOURCESURFACERECORDING_H_
#define MOZILLA_GFX_SOURCESURFACERECORDING_H_

#include "2D.h"
#include "Tools.h"
#include "mozilla/gfx/DrawEventRecorder.h"    // for DrawEventRecorderPrivate
#include "mozilla/RefPtr.h"
#include "mozilla/UniquePtr.h"

namespace mozilla {
namespace gfx {

class SourceSurfaceRecording final : public SourceSurface
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(SourceSurfaceRecording)
  SourceSurfaceRecording(IntSize aSize, SurfaceFormat aFormat, DrawEventRecorderPrivate *aRecorder)
    : mSize(aSize), mFormat(aFormat), mRecorder(aRecorder)
  {
    mRecorder->AddStoredObject(this);
  }

  ~SourceSurfaceRecording();

  virtual SurfaceType GetType() const { return SurfaceType::RECORDING; }
  virtual IntSize GetSize() const { return mSize; }
  virtual SurfaceFormat GetFormat() const { return mFormat; }
  virtual already_AddRefed<DataSourceSurface> GetDataSurface() { return nullptr; }
  DrawEventRecorderPrivate* const GetRecorder() { return mRecorder; }

  IntSize mSize;
  SurfaceFormat mFormat;
  RefPtr<DrawEventRecorderPrivate> mRecorder;
};

class DataSourceSurfaceRecording final : public DataSourceSurface
{
public:
  MOZ_DECLARE_REFCOUNTED_VIRTUAL_TYPENAME(DataSourceSurfaceRecording, override)
  DataSourceSurfaceRecording(UniquePtr<uint8_t[]> aData, IntSize aSize,
                             int32_t aStride, SurfaceFormat aFormat)
    : mData(Move(aData))
    , mSize(aSize)
    , mStride(aStride)
    , mFormat(aFormat)
  {
  }

  ~DataSourceSurfaceRecording()
  {
  }

  static already_AddRefed<DataSourceSurface>
  Init(uint8_t *aData, IntSize aSize, int32_t aStride, SurfaceFormat aFormat)
  {
    //XXX: do we need to ensure any alignment here?
    auto data = MakeUnique<uint8_t[]>(aStride * aSize.height * BytesPerPixel(aFormat));
    if (data) {
      memcpy(data.get(), aData, aStride * aSize.height * BytesPerPixel(aFormat));
      RefPtr<DataSourceSurfaceRecording> surf = new DataSourceSurfaceRecording(Move(data), aSize, aStride, aFormat);
      return surf.forget();
    }
    return nullptr;
  }

  virtual SurfaceType GetType() const override { return SurfaceType::DATA_RECORDING; }
  virtual IntSize GetSize() const override { return mSize; }
  virtual int32_t Stride() override { return mStride; }
  virtual SurfaceFormat GetFormat() const override { return mFormat; }
  virtual uint8_t* GetData() override { return mData.get(); }

  UniquePtr<uint8_t[]> mData;
  IntSize mSize;
  int32_t mStride;
  SurfaceFormat mFormat;
};

} // namespace gfx
} // namespace mozilla

#endif /* MOZILLA_GFX_SOURCESURFACERECORDING_H_ */
