/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "SharedSurfaceBridgeChild.h"
#include "SourceSurfaceSharedData.h"
#include "MainThreadUtils.h"            // for NS_IsMainThread

namespace mozilla {
namespace layers {

using namespace mozilla::gfx;

class SharedUserData final
{
public:
  explicit SharedUserData(SharedSurfaceBridgeChild* aBridge,
                                uint64_t aId)
    : mBridge(aBridge)
    , mId(aId)
    , mShared(false)
  { }

  ~SharedUserData()
  {
    Release();
  }

  uint64_t Id() const
  {
    return mId;
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

  bool UpdateBridge(SharedSurfaceBridgeChild* aBridge)
  {
    if (MOZ_UNLIKELY(aBridge != mBridge)) {
      Unused << NS_WARN_IF(Release());
      mBridge = aBridge;
      return true;
    }
    return false;
  }

private:
  bool Release()
  {
    bool released = false;
    if (mShared) {
      mShared = false;
      MOZ_ASSERT(mBridge);

      // If the channel is closed, then we already freed the image in the WR
      // process.
      if (mBridge->IPCOpen()) {
        mBridge->SendRemove(mId);
        released = true;
      }
    }
    return released;
  }

  RefPtr<SharedSurfaceBridgeChild> mBridge;
  uint64_t mId;
  bool mShared : 1;
};

static void
DestroySharedUserData(void* aClosure)
{
  MOZ_ASSERT(aClosure);

  if (!NS_IsMainThread()) {
    SystemGroup::Dispatch("DestroySharedUserData", TaskCategory::Other,
                          NS_NewRunnableFunction([=]() -> void {
      DestroySharedUserData(aClosure);
    }));
    return;
  }

  SharedUserData* data = static_cast<SharedUserData*>(aClosure);
  delete data;
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

nsresult
SharedSurfaceBridgeChild::Share(SourceSurfaceSharedData* aSurface,
                                uint64_t& aId)
{
  MOZ_ASSERT(NS_IsMainThread());

  auto pid = OtherPid();

  if (pid == base::GetCurrentProcId()) {
    return NS_ERROR_NOT_IMPLEMENTED;
  }

  static UserDataKey sSharedKey;
  SharedUserData* data =
    static_cast<SharedUserData*>(aSurface->GetUserData(&sSharedKey));
  if (!data) {
    data = new SharedUserData(this, GetNextId());
    aSurface->AddUserData(&sSharedKey, data, DestroySharedUserData);
  } else if (MOZ_UNLIKELY(data->UpdateBridge(this))) {
    // FIXME: Is there a better way to clean this up if the bridge gets
    // reinitialized to ensure we release it in a timely manner? Perhaps
    // SurfaceCacheUtils::DiscardAll or something similar for shared images?
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

} // namespace layers
} // namespace mozilla
