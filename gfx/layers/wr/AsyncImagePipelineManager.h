/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H
#define MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H

#include <queue>

#include "CompositableHost.h"
#include "mozilla/gfx/Point.h"
#include "mozilla/layers/EpochScheduler.h"
#include "mozilla/layers/TextureHost.h"
#include "mozilla/Maybe.h"
#include "mozilla/webrender/WebRenderAPI.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsClassHashtable.h"

namespace mozilla {

namespace wr {
class DisplayListBuilder;
class WebRenderAPI;
}

namespace layers {

class CompositableHost;
class CompositorVsyncScheduler;
class WebRenderImageHost;
class WebRenderTextureHost;
class TextureHost;

class EpochTextureHostRunnable final : public EpochRunnable
{
public:
  EpochTextureHostRunnable(const wr::Epoch& aEpoch, TextureHost* aTextureHost)
    : EpochRunnable(aEpoch)
    , mTextureHost(aTextureHost)
  { }

private:
  RefPtr<TextureHost> mTextureHost;
};

class AsyncImagePipelineManager final
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(AsyncImagePipelineManager)

  AsyncImagePipelineManager(already_AddRefed<wr::WebRenderAPI>&& aApi,
                            EpochSchedulerManager* aEpochManager);

protected:
  ~AsyncImagePipelineManager();

public:
  void Destroy();

  void HoldExternalImage(const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch, WebRenderTextureHost* aTexture);

  TimeStamp GetCompositionTime() const {
    return mCompositionTime;
  }
  void SetCompositionTime(TimeStamp aTimeStamp) {
    mCompositionTime = aTimeStamp;
    if (!mCompositionTime.IsNull() && !mCompositeUntilTime.IsNull() &&
        mCompositionTime >= mCompositeUntilTime) {
      mCompositeUntilTime = TimeStamp();
    }
  }
  void CompositeUntil(TimeStamp aTimeStamp) {
    if (mCompositeUntilTime.IsNull() ||
        mCompositeUntilTime < aTimeStamp) {
      mCompositeUntilTime = aTimeStamp;
    }
  }
  TimeStamp GetCompositeUntilTime() const {
    return mCompositeUntilTime;
  }

  void AddAsyncImagePipeline(const wr::PipelineId& aPipelineId, WebRenderImageHost* aImageHost);
  void RemoveAsyncImagePipeline(const wr::PipelineId& aPipelineId);

  void UpdateAsyncImagePipeline(const wr::PipelineId& aPipelineId,
                                const LayoutDeviceRect& aScBounds,
                                const gfx::Matrix4x4& aScTransform,
                                const gfx::MaybeIntSize& aScaleToSize,
                                const wr::ImageRendering& aFilter,
                                const wr::MixBlendMode& aMixBlendMode);
  void ApplyAsyncImages();

  void AppendImageCompositeNotification(const ImageCompositeNotificationInfo& aNotification)
  {
    mImageCompositeNotifications.AppendElement(aNotification);
  }

  void FlushImageNotifications(nsTArray<ImageCompositeNotificationInfo>* aNotifications)
  {
    aNotifications->AppendElements(Move(mImageCompositeNotifications));
  }

  void SetWillGenerateFrame();
  bool GetAndResetWillGenerateFrame();

private:

  uint32_t GetNextResourceId() { return ++mResourceId; }
  wr::IdNamespace GetNamespace() { return mIdNamespace; }
  wr::ImageKey GenerateImageKey()
  {
    wr::ImageKey key;
    key.mNamespace = GetNamespace();
    key.mHandle = GetNextResourceId();
    return key;
  }

  struct AsyncImagePipeline {
    AsyncImagePipeline();
    void Update(const LayoutDeviceRect& aScBounds,
                const gfx::Matrix4x4& aScTransform,
                const gfx::MaybeIntSize& aScaleToSize,
                const wr::ImageRendering& aFilter,
                const wr::MixBlendMode& aMixBlendMode)
    {
      mIsChanged |= !mScBounds.IsEqualEdges(aScBounds) ||
                    mScTransform != aScTransform ||
                    mScaleToSize != aScaleToSize ||
                    mFilter != aFilter ||
                    mMixBlendMode != aMixBlendMode;
      mScBounds = aScBounds;
      mScTransform = aScTransform;
      mScaleToSize = aScaleToSize;
      mFilter = aFilter;
      mMixBlendMode = aMixBlendMode;
    }

    bool mInitialised;
    bool mIsChanged;
    bool mUseExternalImage;
    LayoutDeviceRect mScBounds;
    gfx::Matrix4x4 mScTransform;
    gfx::MaybeIntSize mScaleToSize;
    wr::ImageRendering mFilter;
    wr::MixBlendMode mMixBlendMode;
    RefPtr<WebRenderImageHost> mImageHost;
    RefPtr<EpochScheduler> mEpochScheduler;
    CompositableTextureHostRef mCurrentTexture;
    nsTArray<wr::ImageKey> mKeys;
  };

  Maybe<TextureHost::ResourceUpdateOp>
  UpdateImageKeys(wr::ResourceUpdateQueue& aResourceUpdates,
                  AsyncImagePipeline* aPipeline,
                  nsTArray<wr::ImageKey>& aKeys);
  Maybe<TextureHost::ResourceUpdateOp>
  UpdateWithoutExternalImage(wr::ResourceUpdateQueue& aResources,
                             TextureHost* aTexture,
                             wr::ImageKey aKey,
                             TextureHost::ResourceUpdateOp);

  RefPtr<wr::WebRenderAPI> mApi;
  RefPtr<EpochSchedulerManager> mEpochSchedulerManager;
  wr::IdNamespace mIdNamespace;
  uint32_t mResourceId;

  nsClassHashtable<nsUint64HashKey, AsyncImagePipeline> mAsyncImagePipelines;
  uint32_t mAsyncImageEpoch;
  bool mWillGenerateFrame;
  bool mDestroyed;

  // Render time for the current composition.
  TimeStamp mCompositionTime;

  // When nonnull, during rendering, some compositable indicated that it will
  // change its rendering at this time. In order not to miss it, we composite
  // on every vsync until this time occurs (this is the latest such time).
  TimeStamp mCompositeUntilTime;

  nsTArray<ImageCompositeNotificationInfo> mImageCompositeNotifications;
};

} // namespace layers
} // namespace mozilla

#endif /* MOZILLA_GFX_WEBRENDERCOMPOSITABLE_HOLDER_H */
