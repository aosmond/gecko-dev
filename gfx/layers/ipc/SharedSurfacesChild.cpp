/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfacesChild.h"
#include "SharedSurfacesParent.h"
#include "CompositorManagerChild.h"
#include "mozilla/gfx/SourceSurfaceRecording.h"
#include "mozilla/layers/IpcResourceUpdateQueue.h"
#include "mozilla/layers/SourceSurfaceSharedData.h"
#include "mozilla/layers/WebRenderBridgeChild.h"
#include "mozilla/layers/WebRenderLayerManager.h"
#include "mozilla/SystemGroup.h"        // for SystemGroup

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

UserDataKey SharedSurfacesChild::sSharedKey;

class SharedSurfacesChild::ImageKeyData final
{
public:
  RefPtr<WebRenderLayerManager> mManager;
  wr::ImageKey mImageKey;
  bool mStale;
};

class SharedSurfacesChild::UserData
{
public:
  explicit UserData()
    : mShared(false)
  { }

  virtual ~UserData()
  {
    if (mShared) {
      mShared = false;
      if (NS_IsMainThread()) {
        SharedSurfacesChild::Unshare(GetExternalImageId(), mKeys);
      } else {
        class DestroyRunnable final : public Runnable
        {
        public:
          DestroyRunnable(Maybe<wr::ExternalImageId>&& aId,
                          nsTArray<ImageKeyData>&& aKeys)
            : Runnable("SharedSurfacesChild::SharedUserData::DestroyRunnable")
            , mId(aId)
            , mKeys(aKeys)
          { }

          NS_IMETHOD Run() override
          {
            SharedSurfacesChild::Unshare(mId, mKeys);
            return NS_OK;
          }

        private:
          Maybe<wr::ExternalImageId> mId;
          AutoTArray<ImageKeyData, 1> mKeys;
        };

        nsCOMPtr<nsIRunnable> task =
          new DestroyRunnable(GetExternalImageId(), Move(mKeys));
        SystemGroup::Dispatch(TaskCategory::Other, task.forget());
      }
    }
  }

  bool IsShared() const
  {
    return mShared;
  }

  void MarkShared()
  {
    MOZ_ASSERT(!mShared);
    mShared = true;
  }

  void MarkKeysStale()
  {
    auto i = mKeys.Length();
    while (i > 0) {
      --i;
      ImageKeyData& entry = mKeys[i];
      if (entry.mManager->IsDestroyed()) {
        mKeys.RemoveElementAt(i);
      } else {
        entry.mStale = true;
      }
    }
  }

  wr::ImageKey UpdateKey(WebRenderLayerManager* aManager,
                         wr::IpcResourceUpdateQueue& aResources)
  {
    MOZ_ASSERT(aManager);
    MOZ_ASSERT(!aManager->IsDestroyed());

    // We iterate through all of the items to ensure we clean up the old
    // WebRenderLayerManager references. Most of the time there will be few
    // entries and this should not be particularly expensive compared to the
    // cost of duplicating image keys. In an ideal world, we would generate a
    // single key for the surface, and it would be usable on all of the
    // renderer instances. For now, we must allocate a key for each WR bridge.
    wr::ImageKey key;
    bool found = false;
    auto i = mKeys.Length();
    while (i > 0) {
      --i;
      ImageKeyData& entry = mKeys[i];
      if (entry.mManager->IsDestroyed()) {
        mKeys.RemoveElementAt(i);
      } else if (entry.mManager == aManager) {
        found = true;
        if (entry.mStale) {
          aManager->AddImageKeyForDiscard(entry.mImageKey);
          entry.mImageKey = aManager->WrBridge()->GetNextImageKey();
          entry.mStale = false;
          AddResource(aResources, entry.mImageKey);
        }
        key = entry.mImageKey;
      }
    }

    if (!found) {
      key = aManager->WrBridge()->GetNextImageKey();
      mKeys.AppendElement(ImageKeyData { aManager, key, false });
      AddResource(aResources, key);
    }

    return key;
  }

protected:
  virtual void AddResource(wr::IpcResourceUpdateQueue& aResources,
                           const wr::ImageKey& aKey) const = 0;

  virtual Maybe<wr::ExternalImageId> GetExternalImageId() const
  {
    return Nothing();
  }

  AutoTArray<ImageKeyData, 1> mKeys;
  bool mShared : 1;
};

class SharedSurfacesChild::SharedUserData final
  : public SharedSurfacesChild::UserData
{
public:
  explicit SharedUserData(const wr::ExternalImageId& aId)
    : mId(aId)
  { }

  const wr::ExternalImageId& Id() const
  {
    return mId;
  }

  void SetId(const wr::ExternalImageId& aId)
  {
    mId = aId;
    mKeys.Clear();
    mShared = false;
  }

protected:
  Maybe<wr::ExternalImageId> GetExternalImageId() const override
  {
    return Some(mId);
  }

  void AddResource(wr::IpcResourceUpdateQueue& aResources,
                   const wr::ImageKey& aKey) const override
  {
    aResources.AddExternalImage(mId, aKey);
  }

private:
  wr::ExternalImageId mId;
};

class SharedSurfacesChild::RecordUserData final
  : public SharedSurfacesChild::UserData
{
public:
  RecordUserData(SourceSurfaceRecording* aSurface)
    : mSurface(aSurface)
  {
    MOZ_ASSERT(mSurface);
    MarkShared();
  }

protected:
  void AddResource(wr::IpcResourceUpdateQueue& aResources,
                   const wr::ImageKey& aKey) const override
  {
    DrawEventRecorderPrivate* recorder = mSurface->GetRecorder();
    MOZ_ASSERT(recorder);

    Range<uint8_t> bytes = recorder->GetRange();
    MOZ_ASSERT(bytes.length() > 0);

    wr::ImageDescriptor descriptor(mSurface->GetSize(), 0, mSurface->GetFormat());
    aResources.AddBlobImage(aKey, descriptor, bytes);
  }

private:
  SourceSurfaceRecording* MOZ_OWNING_REF mSurface;
};

/* static */ void
SharedSurfacesChild::DestroySharedUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);
  auto data = static_cast<SharedUserData*>(aClosure);
  delete data;
}

/* static */ void
SharedSurfacesChild::DestroyRecordUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);
  auto data = static_cast<RecordUserData*>(aClosure);
  delete data;
}

/* static */ void
SharedSurfacesChild::SurfaceUpdated(SourceSurface* aSurface)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSurface);

  SurfaceType type = aSurface->GetType();
  if (type != SurfaceType::DATA_SHARED) {
    MOZ_ASSERT(type != SurfaceType::RECORDING, "We should never update a recording!");
    return;
  }

  auto sharedSurface = static_cast<SourceSurfaceSharedData*>(aSurface);
  SharedUserData* data =
    static_cast<SharedUserData*>(sharedSurface->GetUserData(&sSharedKey));
  if (!data) {
    return;
  }

  data->MarkKeysStale();
}

/* static */ nsresult
SharedSurfacesChild::Share(SourceSurface* aSurface,
                           WebRenderLayerManager* aManager,
                           wr::IpcResourceUpdateQueue& aResources,
                           wr::ImageKey& aKey)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aSurface);
  MOZ_ASSERT(aManager);

  switch (aSurface->GetType()) {
    case SurfaceType::DATA_SHARED: {
      auto sharedSurface = static_cast<SourceSurfaceSharedData*>(aSurface);
      return Share(sharedSurface, aManager, aResources, aKey);
    }
    case SurfaceType::RECORDING: {
      auto recordSurface = static_cast<SourceSurfaceRecording*>(aSurface);
      return Share(recordSurface, aManager, aResources, aKey);
    }
    default:
      break;
  }

  return NS_ERROR_NOT_IMPLEMENTED;
}

/* static */ nsresult
SharedSurfacesChild::Share(SourceSurfaceSharedData* aSurface,
                           WebRenderLayerManager* aManager,
                           wr::IpcResourceUpdateQueue& aResources,
                           wr::ImageKey& aKey)
{
  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (NS_WARN_IF(!manager || !manager->CanSend())) {
    return NS_ERROR_NOT_INITIALIZED;
  }

  SharedUserData* data =
    static_cast<SharedUserData*>(aSurface->GetUserData(&sSharedKey));
  if (!data) {
    data = new SharedUserData(manager->GetNextExternalImageId());
    aSurface->AddUserData(&sSharedKey, data, DestroySharedUserData);
  } else if (!manager->OwnsExternalImageId(data->Id())) {
    // If the id isn't owned by us, that means the bridge was reinitialized, due
    // to the GPU process crashing. All previous mappings have been released.
    data->SetId(manager->GetNextExternalImageId());
  } else if (data->IsShared()) {
    // It has already been shared with the GPU process, reuse the id.
    aKey = data->UpdateKey(aManager, aResources);
    return NS_OK;
  }

  // Ensure that the handle doesn't get released until after we have finished
  // sending the buffer to the GPU process and/or reallocating it.
  // FinishedSharing is not a sufficient condition because another thread may
  // decide we are done while we are in the processing of sharing our newly
  // reallocated handle. Once it goes out of scope, it may release the handle.
  SourceSurfaceSharedData::HandleLock lock(aSurface);

  // If we live in the same process, then it is a simple matter of directly
  // asking the parent instance to store a pointer to the same data, no need
  // to map the data into our memory space twice.
  auto pid = manager->OtherPid();
  if (pid == base::GetCurrentProcId()) {
    SharedSurfacesParent::AddSameProcess(data->Id(), aSurface);
    data->MarkShared();
    aKey = data->UpdateKey(aManager, aResources);
    return NS_OK;
  }

  // Attempt to share a handle with the GPU process. The handle may or may not
  // be available -- it will only be available if it is either not yet finalized
  // and/or if it has been finalized but never used for drawing in process.
  ipc::SharedMemoryBasic::Handle handle = ipc::SharedMemoryBasic::NULLHandle();
  nsresult rv = aSurface->ShareToProcess(pid, handle);
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    // It is at least as expensive to copy the image to the GPU process if we
    // have already closed the handle necessary to share, but if we reallocate
    // the shared buffer to get a new handle, we can save some memory.
    if (NS_WARN_IF(!aSurface->ReallocHandle())) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Reattempt the sharing of the handle to the GPU process.
    rv = aSurface->ShareToProcess(pid, handle);
  }

  if (NS_WARN_IF(NS_FAILED(rv))) {
    MOZ_ASSERT(rv != NS_ERROR_NOT_AVAILABLE);
    return rv;
  }

  SurfaceFormat format = aSurface->GetFormat();
  MOZ_RELEASE_ASSERT(format == SurfaceFormat::B8G8R8X8 ||
                     format == SurfaceFormat::B8G8R8A8, "bad format");

  data->MarkShared();
  manager->SendAddSharedSurface(data->Id(),
                                SurfaceDescriptorShared(aSurface->GetSize(),
                                                        aSurface->Stride(),
                                                        format, handle));
  aKey = data->UpdateKey(aManager, aResources);
  return NS_OK;
}

/* static */ nsresult
SharedSurfacesChild::Share(SourceSurfaceRecording* aSurface,
                           WebRenderLayerManager* aManager,
                           wr::IpcResourceUpdateQueue& aResources,
                           wr::ImageKey& aKey)
{
  RecordUserData* data =
    static_cast<RecordUserData*>(aSurface->GetUserData(&sSharedKey));
  if (!data) {
    data = new RecordUserData(aSurface);
    aSurface->AddUserData(&sSharedKey, data, DestroyRecordUserData);
  }

  // There is no extra sharing step with recorded blob images. The key itself
  // was created with the data inlined.
  aKey = data->UpdateKey(aManager, aResources);
  return NS_OK;
}

/* static */ nsresult
SharedSurfacesChild::Share(ImageContainer* aContainer,
                           WebRenderLayerManager* aManager,
                           wr::IpcResourceUpdateQueue& aResources,
                           wr::ImageKey& aKey)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(aContainer);
  MOZ_ASSERT(aManager);

  if (aContainer->IsAsync()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  AutoTArray<ImageContainer::OwningImage,4> images;
  aContainer->GetCurrentImages(&images);
  if (images.IsEmpty()) {
    return NS_ERROR_NOT_AVAILABLE;
  }

  RefPtr<gfx::SourceSurface> surface = images[0].mImage->GetAsSourceSurface();
  if (!surface) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  return Share(surface, aManager, aResources, aKey);
}

/* static */ void
SharedSurfacesChild::Unshare(const Maybe<wr::ExternalImageId>& aId,
                             nsTArray<ImageKeyData>& aKeys)
{
  MOZ_ASSERT(NS_IsMainThread());

  for (const auto& entry : aKeys) {
    entry.mManager->AddImageKeyForDiscard(entry.mImageKey);

    if (aId) {
      WebRenderBridgeChild* wrBridge = entry.mManager->WrBridge();
      if (wrBridge) {
        wrBridge->DeallocExternalImageId(aId.ref());
      }
    }
  }

  if (!aId) {
    return;
  }

  CompositorManagerChild* manager = CompositorManagerChild::GetInstance();
  if (MOZ_UNLIKELY(!manager || !manager->CanSend())) {
    return;
  }

  const auto& id = aId.ref();
  if (manager->OtherPid() == base::GetCurrentProcId()) {
    // We are in the combined UI/GPU process. Call directly to it to remove its
    // wrapper surface to free the underlying buffer.
    MOZ_ASSERT(manager->OwnsExternalImageId(id));
    SharedSurfacesParent::RemoveSameProcess(id);
  } else if (manager->OwnsExternalImageId(id)) {
    // Only attempt to release current mappings in the GPU process. It is
    // possible we had a surface that was previously shared, the GPU process
    // crashed / was restarted, and then we freed the surface. In that case
    // we know the mapping has already been freed.
    manager->SendRemoveSharedSurface(id);
  }
}

} // namespace layers
} // namespace mozilla
