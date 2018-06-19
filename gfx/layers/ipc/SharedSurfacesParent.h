/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_SHAREDSURFACESPARENT_H
#define MOZILLA_GFX_SHAREDSURFACESPARENT_H

#include <stdint.h>                     // for uint32_t
#include "mozilla/Attributes.h"         // for override
#include "mozilla/StaticMutex.h"        // for StaticMutex
#include "mozilla/StaticPtr.h"          // for StaticAutoPtr
#include "mozilla/RefPtr.h"             // for already_AddRefed
#include "mozilla/ipc/SharedMemory.h"   // for SharedMemory, etc
#include "mozilla/gfx/2D.h"             // for SurfaceFormat
#include "mozilla/gfx/Point.h"          // for IntSize
#include "mozilla/layers/LayersSurfaces.h"    // for SurfaceDescriptorShared
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
  static void Initialize();
  static void Shutdown();

  // Get without increasing the consumer count.
  static already_AddRefed<gfx::DataSourceSurface>
  Get(const wr::ExternalImageId& aId);

  // Get but also increase the consumer count. Must call Release after finished.
  static already_AddRefed<gfx::DataSourceSurface>
  Acquire(const wr::ExternalImageId& aId);

  static bool Release(const wr::ExternalImageId& aId);

  static void Add(const wr::ExternalImageId& aId,
                  const SurfaceDescriptorShared& aDesc,
                  base::ProcessId aPid);

  static void Remove(const wr::ExternalImageId& aId);

  static void AddPipeline(const wr::PipelineId& aId,
                          base::ProcessId aPid);

  static void RemovePipeline(const wr::PipelineId& aId,
                             base::ProcessId aPid);

  static Maybe<wr::ExternalImageId>
  AdvancePipeline(const wr::PipelineId& aId,
                  const TimeStamp& aTime);

  static void BindToPipeline(const wr::PipelineId& aId,
                             const wr::ExternalImageId& aImageId,
                             int32_t aFrameTimeout,
                             base::ProcessId aPid);

  static void DestroyProcess(base::ProcessId aPid);

  ~SharedSurfacesParent();

private:
  friend class SharedSurfacesChild;

  static bool ReleaseLocked(const wr::ExternalImageId& aId);

  class PipelineEntry final {
  public:
    PipelineEntry(const wr::ExternalImageId& aId,
                  int32_t aTimeout)
      : mId(Some(aId))
      , mTimeout(aTimeout)
    {
      if (mTimeout < 0) {
        // If the frame lasts forever, we should not be given any more frames
        // in our queue.
        mTimeout = 0;
      }
    }

    ~PipelineEntry()
    {
      if (mId) {
        SharedSurfacesParent::ReleaseLocked(mId.ref());
      }
    }

    PipelineEntry(PipelineEntry&& aOther)
      : mId(std::move(aOther.mId))
      , mTimeout(aOther.mTimeout)
    { }

    PipelineEntry& operator=(PipelineEntry&& aOther)
    {
      mId = std::move(aOther.mId);
      mTimeout = aOther.mTimeout;
      return *this;
    }

    PipelineEntry(const PipelineEntry& aOther) = delete;
    PipelineEntry& operator=(const PipelineEntry& aOther) = delete;

    const Maybe<wr::ExternalImageId>& Id() const { return mId; }
    Maybe<wr::ExternalImageId> TakeId() { return std::move(mId); }
    uint32_t FrameIndex() const { return 0; }

    TimeStamp ExpiryTime(const TimeStamp& aTime) const
    {
      return aTime + TimeDuration::FromMilliseconds(double(mTimeout));
    }

  private:
    Maybe<wr::ExternalImageId> mId;
    int32_t mTimeout;
  };

  class Pipeline final {
  public:
    Pipeline(const wr::PipelineId& aId, const base::ProcessId aPid)
      : mId(aId)
      , mPid(aPid)
      , mDiscarding(false)
      , mHasLastFrame(false)
    { }

    base::ProcessId GetCreatorPid() const { return mPid; }
    const wr::PipelineId& Id() const { return mId; }

    void Add(const wr::ExternalImageId& aId, int32_t aTimeout)
    {
      mEntries.push_back(PipelineEntry(aId, aTimeout));
    }

    Maybe<wr::ExternalImageId> Advance(const TimeStamp& aTime);
    void Reset();
    void SetDiscarding();
    void SetHasLastFrame();
    void MayRecycle(const wr::ExternalImageId& aId);

  private:
    wr::PipelineId mId;
    base::ProcessId mPid;
    bool mDiscarding;
    bool mHasLastFrame;
    Maybe<TimeStamp> mHeadExpiryTime;
    std::deque<PipelineEntry> mEntries;
    std::deque<PipelineEntry> mDiscardEntries;
  };

  SharedSurfacesParent();

  static void AddSameProcess(const wr::ExternalImageId& aId,
                             gfx::SourceSurfaceSharedData* aSurface);
  static void RemoveSameProcess(const wr::ExternalImageId& aId);

  static StaticMutex sMutex;

  static StaticAutoPtr<SharedSurfacesParent> sInstance;

  nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurfaceSharedDataWrapper> mSurfaces;
  nsClassHashtable<nsUint64HashKey, Pipeline> mPipelines;
};

} // namespace layers
} // namespace mozilla

#endif
