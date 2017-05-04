/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeChild.h"
#include "SharedSurfaceBridgeParent.h"
#include "SourceSurfaceSharedData.h"
#include "mozilla/SystemGroup.h"        // for SystemGroup

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

StaticRefPtr<SharedSurfaceBridgeChild> SharedSurfaceBridgeChild::sInstance;

class SharedSurfaceBridgeChild::SharedUserData final
{
public:
  explicit SharedUserData(const wr::ExternalImageId& aId)
    : mId(aId)
    , mShared(false)
  { }

  ~SharedUserData()
  {
    if (mShared) {
      mShared = false;
      if (NS_IsMainThread()) {
        SharedSurfaceBridgeChild::Unshare(mId);
      } else {
        wr::ExternalImageId id = mId;
        SystemGroup::Dispatch("DestroySharedUserData", TaskCategory::Other,
                              NS_NewRunnableFunction([id]() -> void {
          SharedSurfaceBridgeChild::Unshare(id);
        }));
      }
    }
  }

  const wr::ExternalImageId& Id() const
  {
    return mId;
  }

  void SetId(const wr::ExternalImageId& aId)
  {
    mId = aId;
    mShared = false;
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

private:
  wr::ExternalImageId mId;
  bool mShared : 1;
};

/* static */ void
SharedSurfaceBridgeChild::DestroySharedUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);
  auto data = static_cast<SharedUserData*>(aClosure);
  delete data;
}

/* static */ bool
SharedSurfaceBridgeChild::IsInitialized()
{
  return !!sInstance;
}

/* static */ void
SharedSurfaceBridgeChild::InitSameProcess(uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }
  SharedSurfaceBridgeParent::CreateSameProcess();
  sInstance = new SharedSurfaceBridgeChild(aNamespace);
}

/* static */ void
SharedSurfaceBridgeChild::Init(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                               uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }
  sInstance = new SharedSurfaceBridgeChild(Move(aEndpoint), aNamespace);
}

/* static */ void
SharedSurfaceBridgeChild::Reinit(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                                 uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  MOZ_ASSERT(sInstance->mNamespace != aNamespace);
  sInstance = new SharedSurfaceBridgeChild(Move(aEndpoint), aNamespace);
}

/* static */ void
SharedSurfaceBridgeChild::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  if (!sInstance->mClosed) {
    sInstance->Close();
  } else if (sInstance->OtherPid() == base::GetCurrentProcId()) {
    SharedSurfaceBridgeParent::ShutdownSameProcess();
  }
  sInstance = nullptr;
}

/* static */ nsresult
SharedSurfaceBridgeChild::Share(SourceSurfaceSharedData* aSurface,
                                wr::ExternalImageId& aId)
{
  if (!sInstance) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  return sInstance->ShareInternal(aSurface, aId);
}

/* static */ void
SharedSurfaceBridgeChild::Unshare(const wr::ExternalImageId& aId)
{
  if (!sInstance) {
    return;
  }
  sInstance->UnshareInternal(aId);
}

SharedSurfaceBridgeChild::SharedSurfaceBridgeChild(uint32_t aNamespace)
  : mClosed(true)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  SetOtherProcessId(base::GetCurrentProcId());
}

SharedSurfaceBridgeChild::SharedSurfaceBridgeChild(Endpoint<PSharedSurfaceBridgeChild>&& aEndpoint,
                                                   uint32_t aNamespace)
  : mClosed(true)
  , mNamespace(aNamespace)
  , mResourceId(0)
{
  if (aEndpoint.Bind(this)) {
    mClosed = false;
    AddRef();
  }
}

void
SharedSurfaceBridgeChild::DeallocPSharedSurfaceBridgeChild()
{
  MOZ_ASSERT(mClosed);
  Release();
}

void
SharedSurfaceBridgeChild::ActorDestroy(ActorDestroyReason aReason)
{
  mClosed = true;
}

wr::ExternalImageId
SharedSurfaceBridgeChild::GetNextId()
{
  uint64_t id = ++mResourceId;
  MOZ_RELEASE_ASSERT(id != 0);
  id |= (static_cast<uint64_t>(mNamespace) << 32);
  return wr::ToExternalImageId(id);
}

bool
SharedSurfaceBridgeChild::OwnsId(const wr::ExternalImageId& aId) const
{
  return mNamespace == static_cast<uint32_t>(wr::AsUint64(aId) >> 32);
}

nsresult
SharedSurfaceBridgeChild::ShareInternal(SourceSurfaceSharedData* aSurface,
                                        wr::ExternalImageId& aId)
{
  MOZ_ASSERT(NS_IsMainThread());

  static UserDataKey sSharedKey;
  SharedUserData* data =
    static_cast<SharedUserData*>(aSurface->GetUserData(&sSharedKey));
  if (!data) {
    data = new SharedUserData(GetNextId());
    aSurface->AddUserData(&sSharedKey, data, DestroySharedUserData);
  } else if (!OwnsId(data->Id())) {
    // If the ID isn't owned by us, that means the bridge was reinitialized, due
    // to the GPU process crashing. All previous mappings have been released.
    MOZ_ASSERT(OtherPid() != base::GetCurrentProcId());
    data->SetId(GetNextId());
  } else if (data->IsShared()) {
    // If the id is valid, we know it has already been shared. Now we just need
    // to regenerate the image key if it doesn't match the last image this layer
    // has rendered.
    aId = data->Id();
    return NS_OK;
  }

  // Ensure that the handle doesn't get released until after we have finished
  // sending the buffer to WebRender and/or reallocating it. FinishedSharing is
  // not a sufficient condition because another thread may decide we are done
  // while we are in the processing of sharing our newly reallocated handle.
  // Once this goes out of scope, it will request release as well.
  SourceSurfaceSharedData::HandleLock lock(aSurface);

  // If we live in the same process, then it is a simple matter of directly
  // asking the parent instance to store a pointer to the same data, no need
  // to map the data into our memory space twice.
  auto pid = OtherPid();
  if (pid == base::GetCurrentProcId()) {
    SharedSurfaceBridgeParent::AddSameProcess(data->Id(), aSurface);
    data->MarkShared();
    aId = data->Id();
    return NS_OK;
  }

  // Attempt to share a handle with the parent process. The handle may not yet
  // be closed because we may be the first process to want access to the image
  // or the image has yet to be finalized.
  ipc::SharedMemoryBasic::Handle handle = ipc::SharedMemoryBasic::NULLHandle();
  nsresult rv = aSurface->ShareToProcess(pid, handle);
  if (rv == NS_ERROR_NOT_AVAILABLE) {
    // Since we close the handle after we share the image, or after we determine
    // it will not be shared (i.e. imgFrame::Draw was called), we must attempt
    // to realloc the shared buffer and reshare the new file handle.
    if (NS_WARN_IF(!aSurface->ReallocHandle())) {
      return NS_ERROR_OUT_OF_MEMORY;
    }

    // Reattempt the sharing of the handle to the parent process.
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
  aId = data->Id();
  SendAdd(aId, SurfaceDescriptorShared(aSurface->GetSize(),
                                       aSurface->Stride(),
                                       format, handle));
  return NS_OK;
}

void
SharedSurfaceBridgeChild::UnshareInternal(const wr::ExternalImageId& aId)
{
  MOZ_ASSERT(NS_IsMainThread());

  if (OtherPid() == base::GetCurrentProcId()) {
    // We are in the combined UI/GPU process. Call directly to it to remove its
    // wrapper surface to free the underlying buffer.
    MOZ_ASSERT(OwnsId(aId));
    SharedSurfaceBridgeParent::RemoveSameProcess(aId);
  } else if (OwnsId(aId)) {
    // Only attempt to release current mappings in the GPU process. It is
    // possible we had a surface that was previously shared, the GPU process
    // crashed / was restarted, and then we freed the surface. In that case
    // we know the mapping has already been freed.
    SendRemove(aId);
  }
}

} // namespace layers
} // namespace mozilla
