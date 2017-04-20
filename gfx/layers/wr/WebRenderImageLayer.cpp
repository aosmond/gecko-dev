/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "WebRenderImageLayer.h"

#include "gfxPrefs.h"
#include "LayersLogging.h"
#include "mozilla/layers/CompositorBridgeChild.h"
#include "mozilla/layers/ImageClient.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/StackingContextHelper.h"
#include "mozilla/layers/TextureClientRecycleAllocator.h"
#include "mozilla/layers/TextureWrapperImage.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/webrender/WebRenderTypes.h"

namespace mozilla {
namespace layers {

using namespace gfx;

WebRenderImageLayer::WebRenderImageLayer(WebRenderLayerManager* aLayerManager)
  : ImageLayer(aLayerManager, static_cast<WebRenderLayer*>(this))
  , mSharedImageId(0)
  , mImageClientTypeContainer(CompositableType::UNKNOWN)
{
  MOZ_COUNT_CTOR(WebRenderImageLayer);
}

WebRenderImageLayer::~WebRenderImageLayer()
{
  MOZ_COUNT_DTOR(WebRenderImageLayer);
  mPipelineIdRequest.DisconnectIfExists();
  if (mKey.isSome()) {
    WrManager()->AddImageKeyForDiscard(mKey.value());
  }
  if (mExternalImageId.isSome()) {
    WrBridge()->DeallocExternalImageId(mExternalImageId.ref());
  }
}

CompositableType
WebRenderImageLayer::GetImageClientType()
{
  if (mImageClientTypeContainer != CompositableType::UNKNOWN) {
    return mImageClientTypeContainer;
  }

  if (mContainer->IsAsync()) {
    mImageClientTypeContainer = CompositableType::IMAGE_BRIDGE;
    return mImageClientTypeContainer;
  }

  AutoLockImage autoLock(mContainer);

  mImageClientTypeContainer = autoLock.HasImage()
    ? CompositableType::IMAGE : CompositableType::UNKNOWN;
  return mImageClientTypeContainer;
}

already_AddRefed<gfx::SourceSurface>
WebRenderImageLayer::GetAsSourceSurface()
{
  if (!mContainer) {
    return nullptr;
  }
  AutoLockImage autoLock(mContainer);
  Image *image = autoLock.GetImage();
  if (!image) {
    return nullptr;
  }
  RefPtr<gfx::SourceSurface> surface = image->GetAsSourceSurface();
  if (!surface || !surface->IsValid()) {
    return nullptr;
  }
  return surface.forget();
}

void
WebRenderImageLayer::ClearCachedResources()
{
  if (mImageClient) {
    mImageClient->ClearCachedResources();
  }
}

class WrImageLayerUserData final
{
public:
  explicit WrImageLayerUserData(CompositorBridgeChild* aBridge)
    : mBridge(aBridge)
    , mId(0)
  { }

  ~WrImageLayerUserData()
  {
    ReleaseId();
  }

  CompositorBridgeChild* Bridge() const
  {
    return mBridge.get();
  }

  uint64_t Id() const
  {
    return mId;
  }

  void UpdateBridge(CompositorBridgeChild* aBridge)
  {
    Unused << NS_WARN_IF(ReleaseId());
    mBridge = aBridge;
  }

  void Share(uint64_t aId, gfx::SourceSurfaceSharedData* aSurface,
             ipc::SharedMemoryBasic::Handle aHandle)
  {
    MOZ_ASSERT(!mId);
    MOZ_ASSERT(aId);
    MOZ_ASSERT(aSurface);

    mId = aId;

    gfx::IntSize size = aSurface->GetSize();
    SurfaceFormat format = aSurface->GetFormat();
    MOZ_RELEASE_ASSERT(format == SurfaceFormat::B8G8R8X8 ||
                       format == SurfaceFormat::B8G8R8A8, "bad format");

    mBridge->SendAddSharedImage(aId, size, aSurface->Stride(), format, aHandle);
  }

private:
  bool ReleaseId()
  {
    bool released = false;
    if (mId) {
      MOZ_ASSERT(mBridge);

      // If the channel is closed, then we already freed the image in the WR
      // process.
      if (mBridge->IPCOpen()) {
        mBridge->SendRemoveSharedImage(mId);
        released = true;
      }
      mId = 0;
    }
    return released;
  }

  RefPtr<CompositorBridgeChild> mBridge;
  uint64_t mId;
};

static void
DestroyWrImageLayerUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);

  if (!NS_IsMainThread()) {
    SystemGroup::Dispatch("DestroyWrImageLayerUserData", TaskCategory::Other,
                          NS_NewRunnableFunction([=]() -> void {
      DestroyWrImageLayerUserData(aClosure);
    }));
    return;
  }

  WrImageLayerUserData* data = static_cast<WrImageLayerUserData*>(aClosure);
  delete data;
}

void
WebRenderImageLayer::DiscardImageKeyIfShared()
{
  // If the new surface from the image is not shareable, but we previously had
  // shared something, we need to release the image key, because we will try to
  // use an external image next.
  if (mSharedImageId) {
    MOZ_ASSERT(mKey.isSome());
    WrManager()->AddImageKeyForDiscard(mKey.value());
    mKey = Nothing();
    mSharedImageId = 0;
  }
}

bool
WebRenderImageLayer::TryShared(Image* aImage)
{
  RefPtr<gfx::SourceSurface> surface = aImage->GetAsSourceSurface();
  if (!surface || surface->GetType() != SurfaceType::DATA_SHARED) {
    DiscardImageKeyIfShared();
    return false;
  }

  gfx::SourceSurfaceSharedData* sharedSurface =
    static_cast<gfx::SourceSurfaceSharedData*>(surface.get());

  CompositorBridgeChild* bridge = WrBridge()->GetCompositorBridgeChild();
  MOZ_ASSERT(bridge);

  static UserDataKey sWrImageLayer;
  WrImageLayerUserData* data =
    static_cast<WrImageLayerUserData*>(sharedSurface->GetUserData(&sWrImageLayer));
  if (!data) {
    data = new WrImageLayerUserData(bridge);
    sharedSurface->AddUserData(&sWrImageLayer, data, DestroyWrImageLayerUserData);
  } else if (MOZ_UNLIKELY(data->Bridge() != bridge)) {
    // FIXME: Is there a better way to clean this up if the compositor gets
    // reinitialized to ensure we release the bridge in a timely manner? Perhaps
    // SurfaceCacheUtils::DiscardAll or something similar for shared images?
    data->UpdateBridge(bridge);
  } else {
    // If the id is valid, we know it has already been shared. Now we just need
    // to regenerate the image key if it doesn't match the last image this layer
    // has rendered.
    uint64_t id = data->Id();
    if (id) {
      mKey = UpdateImageKey(mKey, mSharedImageId, id,
                            sharedSurface->IsFinalized());
      MOZ_ASSERT(mKey.isSome());
      mSharedImageId = id;
      return true;
    }
  }

  // Ensure that the handle doesn't get released until after we have finished
  // sending the buffer to WebRender and/or reallocating it. FinishedSharing is
  // not a sufficient condition because another thread may decide we are done
  // while we are in the processing of sharing our newly reallocated handle.
  // Once this goes out of scope, it will request release as well.
  SourceSurfaceSharedData::HandleLock lock(sharedSurface);

  // Attempt to share a handle with the parent process. The handle may not yet
  // be closed because we may be the first process to want access to the image
  // or the image has yet to be finalized.
  ipc::SharedMemoryBasic::Handle handle = ipc::SharedMemoryBasic::NULLHandle();
  base::ProcessId pid = bridge->GetParentPid();
  nsresult rv = sharedSurface->ShareToProcess(pid, handle);
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    // Since we close the handle after we share the image, or after we determine
    // it will not be shared (i.e. imgFrame::Draw was called), we must attempt
    // to realloc the shared buffer and reshare the new file handle.
    if (NS_WARN_IF(!sharedSurface->ReallocHandle())) {
      DiscardImageKeyIfShared();
      return false;
    }

    // Reattempt the sharing of the handle to the parent process.
    rv = sharedSurface->ShareToProcess(pid, handle);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_ASSERT(rv != NS_ERROR_NOT_AVAILABLE);
    DiscardImageKeyIfShared();
    return false;
  }

  uint64_t id = bridge->GetNextSharedImageId();
  MOZ_ASSERT(id);
  data->Share(id, sharedSurface, handle);
  mKey = UpdateImageKey(mKey, mSharedImageId, id, sharedSurface->IsFinalized());
  MOZ_ASSERT(mKey.isSome());
  mSharedImageId = id;
  return true;
}

bool
WebRenderImageLayer::TryExternal()
{
  CompositableType type = GetImageClientType();
  if (type == CompositableType::UNKNOWN) {
    return false;
  }

  MOZ_ASSERT(GetImageClientType() != CompositableType::UNKNOWN);

  // Allocate PipelineId if necessary
  if (GetImageClientType() == CompositableType::IMAGE_BRIDGE &&
      mPipelineId.isNothing() && !mPipelineIdRequest.Exists()) {
    // Use Holder to pass this pointer to lambda.
    // Static anaysis tool does not permit to pass refcounted variable to lambda.
    // And we do not want to use RefPtr<WebRenderImageLayer> here.
    Holder holder(this);
    WrManager()->AllocPipelineId()
      ->Then(AbstractThread::GetCurrent(), __func__,
      [holder] (const wr::PipelineId& aPipelineId) {
        holder->mPipelineIdRequest.Complete();
        holder->mPipelineId = Some(aPipelineId);
      },
      [holder] (const ipc::PromiseRejectReason &aReason) {
        holder->mPipelineIdRequest.Complete();
      })->Track(mPipelineIdRequest);
  }

  if (GetImageClientType() == CompositableType::IMAGE && !mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return false;
    }
    mImageClient->Connect();
  }

  if (mExternalImageId.isNothing()) {
    if (GetImageClientType() == CompositableType::IMAGE_BRIDGE) {
      MOZ_ASSERT(!mImageClient);
      mExternalImageId = Some(WrBridge()->AllocExternalImageId(mContainer->GetAsyncContainerHandle()));
    } else {
      // Handle CompositableType::IMAGE case
      MOZ_ASSERT(mImageClient);
      mExternalImageId = Some(WrBridge()->AllocExternalImageIdForCompositable(mImageClient));
    }
  }
  MOZ_ASSERT(mExternalImageId.isSome());

  if (GetImageClientType() == CompositableType::IMAGE_BRIDGE) {
    // Always allocate key
    WrImageKey key = GetImageKey();
    WrBridge()->AddWebRenderParentCommand(OpAddExternalImage(mExternalImageId.value(), key));
    WrManager()->AddImageKeyForDiscard(key);
    mKey = Some(key);
  } else {
    // Handle CompositableType::IMAGE case
    MOZ_ASSERT(mImageClient->AsImageClientSingle());
    mKey = UpdateImageKey(mImageClient->AsImageClientSingle(),
                          mContainer,
                          mKey,
                          mExternalImageId.ref());
  }

  if (mKey.isNothing()) {
    return false;
  }

  return true;
}

void
WebRenderImageLayer::RenderLayer(wr::DisplayListBuilder& aBuilder)
{
  if (!mContainer) {
     return;
  }

  // XXX Not good for async ImageContainer case.
  AutoLockImage autoLock(mContainer);
  Image* image = autoLock.GetImage();
  if (!image) {
    return;
  }

  if (!TryShared(image) && !TryExternal()) {
    return;
  }

  StackingContextHelper sc(aBuilder, this);

  gfx::IntSize size = image->GetSize();
  LayerRect rect(0, 0, size.width, size.height);
  if (mScaleMode != ScaleMode::SCALE_NONE) {
    NS_ASSERTION(mScaleMode == ScaleMode::STRETCH,
                 "No other scalemodes than stretch and none supported yet.");
    rect = LayerRect(0, 0, mScaleToSize.width, mScaleToSize.height);
  }

  LayerRect clipRect = ClipRect().valueOr(rect);
  Maybe<WrImageMask> mask = BuildWrMaskLayer(true);
  WrClipRegion clip = aBuilder.BuildClipRegion(
      sc.ToRelativeWrRect(clipRect),
      mask.ptrOr(nullptr));

  wr::ImageRendering filter = wr::ToImageRendering(mSamplingFilter);

  DumpLayerInfo("Image Layer", rect);
  if (gfxPrefs::LayersDump()) {
    printf_stderr("ImageLayer %p texture-filter=%s \n",
                  GetLayer(),
                  Stringify(filter).c_str());
  }

  aBuilder.PushImage(sc.ToRelativeWrRect(rect), clip, filter, mKey.value());
}

bool
WebRenderImageLayer::TryMaskExternal()
{
  CompositableType type = GetImageClientType();
  if (type == CompositableType::UNKNOWN) {
    return false;
  }

  MOZ_ASSERT(GetImageClientType() == CompositableType::IMAGE);
  if (GetImageClientType() != CompositableType::IMAGE) {
    return false;
  }

  if (!mImageClient) {
    mImageClient = ImageClient::CreateImageClient(CompositableType::IMAGE,
                                                  WrBridge(),
                                                  TextureFlags::DEFAULT);
    if (!mImageClient) {
      return false;
    }
    mImageClient->Connect();
  }

  if (mExternalImageId.isNothing()) {
    mExternalImageId = Some(WrBridge()->AllocExternalImageIdForCompositable(mImageClient));
  }

  MOZ_ASSERT(mImageClient->AsImageClientSingle());
  mKey = UpdateImageKey(mImageClient->AsImageClientSingle(),
                        mContainer,
                        mKey,
                        mExternalImageId.ref());

  if (mKey.isNothing()) {
    return false;
  }

  return true;
}

Maybe<WrImageMask>
WebRenderImageLayer::RenderMaskLayer(const gfx::Matrix4x4& aTransform)
{
  if (!mContainer) {
     return Nothing();
  }

  AutoLockImage autoLock(mContainer);
  Image* image = autoLock.GetImage();
  if (!image) {
    return Nothing();
  }

  if (!TryShared(image) && !TryMaskExternal()) {
    return Nothing();
  }

  gfx::IntSize size = image->GetSize();
  WrImageMask imageMask;
  imageMask.image = mKey.value();
  Rect maskRect = aTransform.TransformBounds(Rect(0, 0, size.width, size.height));
  imageMask.rect = wr::ToWrRect(maskRect);
  imageMask.repeat = false;
  return Some(imageMask);
}

} // namespace layers
} // namespace mozilla
