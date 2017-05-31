/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "Image.h"
#include "Layers.h"
#include "nsRefreshDriver.h"
#include "mozilla/TimeStamp.h"

namespace mozilla {

using namespace layers;
using namespace gfx;

namespace image {

///////////////////////////////////////////////////////////////////////////////
// Memory Reporting
///////////////////////////////////////////////////////////////////////////////

ImageMemoryCounter::ImageMemoryCounter(Image* aImage,
                                       MallocSizeOf aMallocSizeOf,
                                       bool aIsUsed)
  : mIsUsed(aIsUsed)
{
  MOZ_ASSERT(aImage);

  // Extract metadata about the image.
  RefPtr<ImageURL> imageURL(aImage->GetURI());
  if (imageURL) {
    imageURL->GetSpec(mURI);
  }

  int32_t width = 0;
  int32_t height = 0;
  aImage->GetWidth(&width);
  aImage->GetHeight(&height);
  mIntrinsicSize.SizeTo(width, height);

  mType = aImage->GetType();

  // Populate memory counters for source and decoded data.
  mValues.SetSource(aImage->SizeOfSourceWithComputedFallback(aMallocSizeOf));
  aImage->CollectSizeOfSurfaces(mSurfaces, aMallocSizeOf);

  // Compute totals.
  for (const SurfaceMemoryCounter& surfaceCounter : mSurfaces) {
    mValues += surfaceCounter.Values();
  }
}


///////////////////////////////////////////////////////////////////////////////
// Image Base Types
///////////////////////////////////////////////////////////////////////////////

Image::Image()
  : mImageProducerID(ImageContainer::AllocateProducerID())
  , mLastFrameID(0)
{ }

// Constructor
ImageResource::ImageResource(ImageURL* aURI) :
  mURI(aURI),
  mInnerWindowId(0),
  mAnimationConsumers(0),
  mAnimationMode(kNormalAnimMode),
  mInitialized(false),
  mAnimating(false),
  mError(false),
{ }

ImageResource::~ImageResource()
{
  // Ask our ProgressTracker to drop its weak reference to us.
  mProgressTracker->ResetImage();
}

void
ImageResource::IncrementAnimationConsumers()
{
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only to encourage serialization "
                                "with DecrementAnimationConsumers");
  mAnimationConsumers++;
}

void
ImageResource::DecrementAnimationConsumers()
{
  MOZ_ASSERT(NS_IsMainThread(), "Main thread only to encourage serialization "
                                "with IncrementAnimationConsumers");
  MOZ_ASSERT(mAnimationConsumers >= 1,
             "Invalid no. of animation consumers!");
  mAnimationConsumers--;
}

nsresult
ImageResource::GetAnimationModeInternal(uint16_t* aAnimationMode)
{
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  NS_ENSURE_ARG_POINTER(aAnimationMode);

  *aAnimationMode = mAnimationMode;
  return NS_OK;
}

nsresult
ImageResource::SetAnimationModeInternal(uint16_t aAnimationMode)
{
  if (mError) {
    return NS_ERROR_FAILURE;
  }

  NS_ASSERTION(aAnimationMode == kNormalAnimMode ||
               aAnimationMode == kDontAnimMode ||
               aAnimationMode == kLoopOnceAnimMode,
               "Wrong Animation Mode is being set!");

  mAnimationMode = aAnimationMode;

  return NS_OK;
}

bool
ImageResource::HadRecentRefresh(const TimeStamp& aTime)
{
  // Our threshold for "recent" is 1/2 of the default refresh-driver interval.
  // This ensures that we allow for frame rates at least as fast as the
  // refresh driver's default rate.
  static TimeDuration recentThreshold =
      TimeDuration::FromMilliseconds(nsRefreshDriver::DefaultInterval() / 2.0);

  if (!mLastRefreshTime.IsNull() &&
      aTime - mLastRefreshTime < recentThreshold) {
    return true;
  }

  // else, we can proceed with a refresh.
  // But first, update our last refresh time:
  mLastRefreshTime = aTime;
  return false;
}

void
ImageResource::EvaluateAnimation()
{
  if (!mAnimating && ShouldAnimate()) {
    nsresult rv = StartAnimation();
    mAnimating = NS_SUCCEEDED(rv);
  } else if (mAnimating && !ShouldAnimate()) {
    StopAnimation();
  }
}

void
ImageResource::SendOnUnlockedDraw(uint32_t aFlags)
{
  if (!mProgressTracker) {
    return;
  }

  if (!(aFlags & FLAG_ASYNC_NOTIFY)) {
    mProgressTracker->OnUnlockedDraw();
  } else {
    NotNull<RefPtr<ImageResource>> image = WrapNotNull(this);
    NS_DispatchToMainThread(NS_NewRunnableFunction([=]() -> void {
      RefPtr<ProgressTracker> tracker = image->GetProgressTracker();
      if (tracker) {
        tracker->OnUnlockedDraw();
      }
    }));
  }
}

already_AddRefed<ImageContainer>
Image::GetImageContainerInternal(LayerManager* aManager, const IntSize& aSize, uint32_t aFlags)
{
  MOZ_ASSERT(aManager);
  MOZ_ASSERT((aFlags & ~(FLAG_SYNC_DECODE |
                         FLAG_SYNC_DECODE_IF_FAST |
                         FLAG_ASYNC_NOTIFY |
                         FLAG_HIGH_QUALITY_SCALING))
               == FLAG_NONE,
             "Unsupported flag passed to GetImageContainer");

  if (!IsImageContainerAvailable(aManager, aFlags)) {
    printf_stderr("[AO] [%p] ImageResource::GetImageContainerInternal -- n/a\n", this);
    return nullptr;
  }

  if (IsUnlocked()) {
    SendOnUnlockedDraw(aFlags);
  }

  RefPtr<ImageContainer> container;
  ImageContainerEntry* entry = nullptr;
  int i = mImageContainers.Length() - 1;
  for (; i >= 0; --i) {
    entry = &mImageContainers[i];
    container = entry->mContainer.get();
    if (aSize == entry->mSize) {
      if (!container) {
        // Force recreation of the image container and fetching of the surface.
        entry->mLastDrawResult = DrawResult::NOT_READY;
      }
      break;
    } else if (!container) {
      // Stop tracking if our weak pointer to the image container was freed.
      mImageContainers.RemoveElementAt(i);
    }
  }

  if (i >= 0) {
    MOZ_ASSERT(entry);
    switch (entry->mLastDrawResult) {
      case DrawResult::SUCCESS:
        // The container already contains the latest image.
        return container.forget();
      case DrawResult::NOT_READY:
      case DrawResult::INCOMPLETE:
      case DrawResult::TEMPORARY_ERROR:
        // Temporary conditions we need to redraw to recover from.
        break;
      case DrawResult::BAD_IMAGE:
      case DrawResult::BAD_ARGS:
      case DrawResult::WRONG_SIZE:
        printf_stderr("[AO] [%p] ImageResource::GetImageContainerInternal -- bad draw\n", this);
        return nullptr;
      default:
        MOZ_ASSERT_UNREACHABLE("Unhandled DrawResult type!");
        return nullptr;
    }
  }

  DrawResult result;
  RefPtr<SourceSurface> surface;
  Tie(result, surface) = GetFrameInternal(aSize, FRAME_CURRENT, aFlags);
  if (!surface) {
    // The OS threw out some or all of our buffer. We'll need to wait for the
    // redecode (which was automatically triggered by GetFrame) to complete.
    MOZ_ASSERT(result != DrawResult::SUCCESS);
    if (entry) {
      entry->mLastDrawResult = result;
    }
    printf_stderr("[AO] [%p] ImageResource::GetImageContainerInternal -- no surface\n", this);
    return nullptr;
  }

  if (!container) {
    container = LayerManager::CreateImageContainer();
  }

  if (entry) {
    entry->mLastDrawResult = result;
  } else {
    entry = mImageContainers.AppendElement(
      ImageContainerEntry(aSize, container.get(), result));
  }

  // |image| holds a reference to a SourceSurface which in turn holds a lock on
  // the current frame's data buffer, ensuring that it doesn't get freed as
  // long as the layer system keeps this ImageContainer alive.
  RefPtr<layers::Image> image = new layers::SourceSurfaceImage(surface);

  // We can share the producer ID with other containers because it is only
  // used internally to validate the frames given to a particular container
  // so that another object cannot add its own. Similarly the frame ID is
  // only used internally to ensure it is always increasing, and skipping
  // IDs from an individual container's perspective is acceptable.
  AutoTArray<ImageContainer::NonOwningImage, 1> imageList;
  imageList.AppendElement(ImageContainer::NonOwningImage(image, TimeStamp(),
                                                         mLastFrameID++,
                                                         mImageProducerID));
  container->SetCurrentImagesInTransaction(imageList);

  return container.forget();
}

void
Image::UpdateImageContainers()
{
  for (int i = mImageContainers.Length() - 1; i >= 0; --i) {
    ImageContainerEntry& entry = mImageContainers[i];
    RefPtr<ImageContainer> container = entry.mContainer.get();
    if (container) {
      DrawResult result;
      RefPtr<SourceSurface> surface;
      Tie(result, surface) =
        GetFrameInternal(entry.mSize, FRAME_CURRENT, FLAG_NONE);

      entry.mLastDrawResult = result;
      if (!surface) {
        MOZ_ASSERT(result != DrawResult::SUCCESS);
        container->ClearAllImages();
        continue;
      }

      // We can share the producer ID with other containers because it is only
      // used internally to validate the frames given to a particular container
      // so that another object cannot add its own. Similarly the frame ID is
      // only used internally to ensure it is always increasing, and skipping
      // IDs from an individual container's perspective is acceptable.
      RefPtr<layers::Image> image = new layers::SourceSurfaceImage(surface);
      AutoTArray<ImageContainer::NonOwningImage, 1> imageList;
      imageList.AppendElement(ImageContainer::NonOwningImage(image, TimeStamp(),
                                                             mLastFrameID++,
                                                             mImageProducerID));
      container->SetCurrentImages(imageList);
    } else {
      // Stop tracking if our weak pointer to the image container was freed.
      mImageContainers.RemoveElementAt(i);
    }
  }
}

} // namespace image
} // namespace mozilla
