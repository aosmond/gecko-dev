/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderUserData.h"

#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/SharedSurfacesChild.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/layers/WebRenderMessages.h"
#include "nsDisplayListInvalidation.h"
#include "WebRenderCanvasRenderer.h"

namespace mozilla {
namespace layers {

WebRenderUserData::WebRenderUserData(WebRenderLayerManager* aWRManager)
  : mWRManager(aWRManager)
{
}

WebRenderUserData::~WebRenderUserData()
{
}

WebRenderBridgeChild*
WebRenderUserData::WrBridge() const
{
  return mWRManager->WrBridge();
}

WebRenderImageData::WebRenderImageData(WebRenderLayerManager* aWRManager)
  : WebRenderUserData(aWRManager)
  , mLastGenerationId(0)
  , mUsingSharedSurface(false)
{
}

WebRenderImageData::~WebRenderImageData()
{
  if (mKey) {
    mWRManager->AddImageKeyForDiscard(mKey.value());
  }

  if (!mUsingSharedSurface && mExternalImageId) {
    WrBridge()->DeallocExternalImageId(mExternalImageId.ref());
  }

  if (mPipelineId) {
    WrBridge()->RemovePipelineIdForCompositable(mPipelineId.ref());
  }
}

void
WebRenderImageData::DiscardKeyIfShared()
{
  if (mUsingSharedSurface) {
    // No need to free the external image ID if it is a shared image. The
    // surface itself owns it.
    MOZ_ASSERT(mExternalImageId.isSome());
    mExternalImageId = Nothing();

    // We are responsible for freeing the ImageKey however.
    if (mKey.isSome()) {
      mWRManager->AddImageKeyForDiscard(mKey.value());
      mKey = Nothing();
    }

    mUsingSharedSurface = false;
  }
}

bool
WebRenderImageData::UpdateImageKeyIfShared(ImageContainer* aContainer, bool aForceUpdate)
{
  AutoTArray<ImageContainer::OwningImage,4> images;
  uint32_t generation;
  aContainer->GetCurrentImages(&images, &generation);
  if (images.IsEmpty()) {
    DiscardKeyIfShared();
    return false;
  }

  Image* image = images[0].mImage.get();
  MOZ_ASSERT(image);

  RefPtr<gfx::SourceSurface> surface = image->GetAsSourceSurface();
  if (!surface || surface->GetType() != SurfaceType::DATA_SHARED) {
    DiscardKeyIfShared();
    return false;
  }

  wr::ExternalImageId id;
  auto sharedSurf = static_cast<gfx::SourceSurfaceSharedData*>(surface.get());
  nsresult rv = SharedSurfacesChild::Share(sharedSurf, id);
  if (NS_FAILED(rv)) {
    DiscardKeyIfShared();
    return false;
  }

  if (mExternalImageId.isSome()) {
    if (mUsingSharedSurface) {
      // If we have a new shared surface, we need to discard the old ImageKey
      // bound to the old surface, and replace the external image ID to generate
      // a new key.
      if (aForceUpdate || generation != mLastGenerationId ||
          !(mExternalImageId.ref() == id)) {
        DiscardKeyIfShared();
      }
    } else {
      // Last image used was an external image, but we are now switching to a
      // shared surface -- since we are the owner of the external image ID, we
      // need to discard it before using the new ID, along with the ImageKey
      // paired with it.
      if (mKey.isSome()) {
        mWRManager->AddImageKeyForDiscard(mKey.value());
        mKey = Nothing();
      }
      WrBridge()->DeallocExternalImageId(mExternalImageId.ref());
      mExternalImageId = Nothing();
    }
  }

  if (mExternalImageId.isNothing()) {
    mExternalImageId.emplace(id);
  } else {
    MOZ_ASSERT(mExternalImageId.ref() == id);
  }

  if (mKey.isNothing()) {
    WrImageKey key = WrBridge()->GetNextImageKey();
    mWRManager->WrBridge()->AddWebRenderParentCommand(OpAddSharedSurface(mExternalImageId.value(), key));
    mKey = Some(key);
  }

  mLastGenerationId = generation;
  mUsingSharedSurface = true;
  return true;
}

Maybe<wr::ImageKey>
WebRenderImageData::UpdateImageKey(ImageContainer* aContainer, bool aForceUpdate)
{
  if (UpdateImageKeyIfShared(aContainer, aForceUpdate)) {
    return mKey;
  }

  CreateImageClientIfNeeded();
  CreateExternalImageIfNeeded();

  if (!mImageClient || !mExternalImageId) {
    return Nothing();
  }

  MOZ_ASSERT(mImageClient->AsImageClientSingle());
  MOZ_ASSERT(aContainer);

  ImageClientSingle* imageClient = mImageClient->AsImageClientSingle();
  uint32_t oldCounter = imageClient->GetLastUpdateGenerationCounter();

  bool ret = imageClient->UpdateImage(aContainer, /* unused */0);
  if (!ret || imageClient->IsEmpty()) {
    // Delete old key
    if (mKey) {
      mWRManager->AddImageKeyForDiscard(mKey.value());
      mKey = Nothing();
    }
    return Nothing();
  }

  // Reuse old key if generation is not updated.
  if (!aForceUpdate && oldCounter == imageClient->GetLastUpdateGenerationCounter() && mKey) {
    return mKey;
  }

  // Delete old key, we are generating a new key.
  if (mKey) {
    mWRManager->AddImageKeyForDiscard(mKey.value());
  }

  wr::WrImageKey key = WrBridge()->GetNextImageKey();
  mWRManager->WrBridge()->AddWebRenderParentCommand(OpAddExternalImage(mExternalImageId.value(), key));
  mKey = Some(key);

  return mKey;
}

already_AddRefed<ImageClient>
WebRenderImageData::GetImageClient()
{
  RefPtr<ImageClient> imageClient = mImageClient;
  return imageClient.forget();
}

void
WebRenderImageData::CreateAsyncImageWebRenderCommands(mozilla::wr::DisplayListBuilder& aBuilder,
                                                      ImageContainer* aContainer,
                                                      const StackingContextHelper& aSc,
                                                      const LayerRect& aBounds,
                                                      const LayerRect& aSCBounds,
                                                      const gfx::Matrix4x4& aSCTransform,
                                                      const gfx::MaybeIntSize& aScaleToSize,
                                                      const wr::ImageRendering& aFilter,
                                                      const wr::MixBlendMode& aMixBlendMode)
{
  MOZ_ASSERT(aContainer->IsAsync());
  if (!mPipelineId) {
    // Alloc async image pipeline id.
    mPipelineId = Some(WrBridge()->GetCompositorBridgeChild()->GetNextPipelineId());
    WrBridge()->AddPipelineIdForAsyncCompositable(mPipelineId.ref(),
                                                  aContainer->GetAsyncContainerHandle());
  }
  MOZ_ASSERT(!mImageClient);
  MOZ_ASSERT(!mExternalImageId);

  // Push IFrame for async image pipeline.
  //
  // We don't push a stacking context for this async image pipeline here.
  // Instead, we do it inside the iframe that hosts the image. As a result,
  // a bunch of the calculations normally done as part of that stacking
  // context need to be done manually and pushed over to the parent side,
  // where it will be done when we build the display list for the iframe.
  // That happens in AsyncImagePipelineManager.
  wr::LayoutRect r = aSc.ToRelativeLayoutRect(aBounds);
  aBuilder.PushIFrame(r, mPipelineId.ref());

  WrBridge()->AddWebRenderParentCommand(OpUpdateAsyncImagePipeline(mPipelineId.value(),
                                                                   aSCBounds,
                                                                   aSCTransform,
                                                                   aScaleToSize,
                                                                   aFilter,
                                                                   aMixBlendMode));
}

void
WebRenderImageData::CreateImageClientIfNeeded()
{
  MOZ_ASSERT(!mUsingSharedSurface);

  if (!mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return;
    }

    mImageClient->Connect();
  }
}

void
WebRenderImageData::CreateExternalImageIfNeeded()
{
  MOZ_ASSERT(!mUsingSharedSurface);

  if (!mExternalImageId)  {
    mExternalImageId = Some(WrBridge()->AllocExternalImageIdForCompositable(mImageClient));
  }
}

WebRenderFallbackData::WebRenderFallbackData(WebRenderLayerManager* aWRManager)
  : WebRenderImageData(aWRManager)
  , mInvalid(false)
{
}

WebRenderFallbackData::~WebRenderFallbackData()
{
}

nsAutoPtr<nsDisplayItemGeometry>
WebRenderFallbackData::GetGeometry()
{
  return mGeometry;
}

void
WebRenderFallbackData::SetGeometry(nsAutoPtr<nsDisplayItemGeometry> aGeometry)
{
  mGeometry = aGeometry;
}

WebRenderAnimationData::WebRenderAnimationData(WebRenderLayerManager* aWRManager)
  : WebRenderUserData(aWRManager),
    mAnimationInfo(aWRManager)
{
}

WebRenderCanvasData::WebRenderCanvasData(WebRenderLayerManager* aWRManager)
  : WebRenderUserData(aWRManager)
{
}

WebRenderCanvasData::~WebRenderCanvasData()
{
}

WebRenderCanvasRendererAsync*
WebRenderCanvasData::GetCanvasRenderer()
{
  if (!mCanvasRenderer) {
    mCanvasRenderer = MakeUnique<WebRenderCanvasRendererAsync>(mWRManager);
  }

  return mCanvasRenderer.get();
}

} // namespace layers
} // namespace mozilla
