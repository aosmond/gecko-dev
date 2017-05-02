/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeChild.h"
#include "SharedSurfaceBridgeParent.h"
#include "SourceSurfaceSharedData.h"
#include "mozilla/StaticPtr.h"          // for StaticRefPtr
#include "MainThreadUtils.h"            // for NS_IsMainThread

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

StaticRefPtr<SharedSurfaceBridgeChild> SharedSurfaceBridgeChild::sInstance;

class SharedSurfaceBridgeChild::SharedUserData final
{
public:
  explicit SharedUserData(uint64_t aId)
    : mId(aId)
    , mShared(false)
  { }

  ~SharedUserData()
  {
    if (mShared) {
      SharedSurfaceBridgeChild::Unshare(mId);
      mShared = false;
    }
  }

  uint64_t Id() const
  {
    return mId;
  }

  void SetId(uint64_t aId)
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
  uint64_t mId;
  bool mShared : 1;
};

/* static */ void
SharedSurfaceBridgeChild::DestroySharedUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);

  if (!NS_IsMainThread()) {
    SystemGroup::Dispatch("DestroySharedUserData", TaskCategory::Other,
                          NS_NewRunnableFunction([=]() -> void {
      DestroySharedUserData(aClosure);
    }));
    return;
  }

  auto data = static_cast<SharedUserData*>(aClosure);
  delete data;
}

/* static */ void
SharedSurfaceBridgeChild::Init(uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  if (NS_WARN_IF(sInstance)) {
    MOZ_ASSERT_UNREACHABLE("Already initalized");
    return;
  }
  sInstance = new SharedSurfaceBridgeChild(aNamespace);
}

/* static */ void
SharedSurfaceBridgeChild::Reinit(uint32_t aNamespace)
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  MOZ_ASSERT(sInstance->mNamespace != aNamespace);
  sInstance = new SharedSurfaceBridgeChild(aNamespace);
}

/* static */ void
SharedSurfaceBridgeChild::Shutdown()
{
  MOZ_ASSERT(NS_IsMainThread());
  MOZ_ASSERT(sInstance);
  sInstance = nullptr;
}

/* static */ nsresult
SharedSurfaceBridgeChild::Share(SourceSurfaceSharedData* aSurface,
                                uint64_t& aId)
{
  if (!sInstance) {
    return NS_ERROR_NOT_INITIALIZED;
  }
  return sInstance->ShareInternal(aSurface, aId);
}

/* static */ void
SharedSurfaceBridgeChild::Unshare(uint64_t aId)
{
  if (!sInstance) {
    return;
  }
  sInstance->UnshareInternal(aId);
}

SharedSurfaceBridgeChild::SharedSurfaceBridgeChild(uint32_t aNamespace)
  : mNamespace(aNamespace)
{
}

uint64_t
SharedSurfaceBridgeChild::GetNextId()
{
  uint64_t id = ++mResourceId;
  MOZ_RELEASE_ASSERT(id != 0);
  id |= (static_cast<uint64_t>(mNamespace) << 32);
  return id;
}

bool
SharedSurfaceBridgeChild::OwnsId(uint64_t aId) const
{
  return mNamespace == (aId >> 32);
}

nsresult
SharedSurfaceBridgeChild::ShareInternal(SourceSurfaceSharedData* aSurface,
                                        uint64_t& aId)
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
  // asking the parent instance to store a pointer to the same surface, no need
  // to map the data into our memory space twice.
  auto pid = OtherPid();
  if (pid == base::GetCurrentProcId()) {
    RefPtr<DataSourceSurface> surf = aSurface;
    SharedSurfaceBridgeParent::Insert(data->Id(), surf.forget());
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

  gfx::IntSize size = aSurface->GetSize();
  SurfaceFormat format = aSurface->GetFormat();
  MOZ_RELEASE_ASSERT(format == SurfaceFormat::B8G8R8X8 ||
                     format == SurfaceFormat::B8G8R8A8, "bad format");

  data->MarkShared();
  aId = data->Id();
  SendAdd(aId, size, aSurface->Stride(), format, handle);
  return NS_OK;
}

void
SharedSurfaceBridgeChild::UnshareInternal(uint64_t aId)
{
  if (OtherPid() == base::GetCurrentProcId()) {
    MOZ_ASSERT(OwnsId(aId));
    SharedSurfaceBridgeParent::Remove(aId);
  } else if (OwnsId(aId)) {
    // Only attempt to release current mappings in the GPU process.
    SendRemove(aId);
  }
}

void
SharedSurfaceBridgeChild::ActorDestroy(ActorDestroyReason aReason)
{
}

} // namespace layers
} // namespace mozilla
