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

class SharedSurfacesMemoryReport final
{
public:
  class SurfaceEntry final {
  public:
    gfx::IntSize mSize;
    int32_t mStride;
    uint32_t mConsumers;
  };

  nsDataHashtable<nsUint64HashKey, SurfaceEntry> mSurfaces;
};

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

  static void DestroyProcess(base::ProcessId aPid);

  static void AccumulateMemoryReport(base::ProcessId aPid,
                                     SharedSurfacesMemoryReport& aReport);

  ~SharedSurfacesParent();

private:
  friend class SharedSurfacesChild;

  SharedSurfacesParent();

  static void AddSameProcess(const wr::ExternalImageId& aId,
                             gfx::SourceSurfaceSharedData* aSurface);
  static void RemoveSameProcess(const wr::ExternalImageId& aId);

  static StaticMutex sMutex;

  static StaticAutoPtr<SharedSurfacesParent> sInstance;

  nsRefPtrHashtable<nsUint64HashKey, gfx::SourceSurfaceSharedDataWrapper> mSurfaces;
};

} // namespace layers
} // namespace mozilla

namespace IPC {

template<class KeyClass, class DataType>
struct ParamTraits<nsDataHashtable<KeyClass, DataType>>
{
  typedef nsDataHashtable<KeyClass, DataType> paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.Count());
    for (auto i = aParam.ConstIter(); !i.Done(); i.Next()) {
      WriteParam(aMsg, i.Key());
      WriteParam(aMsg, i.Data());
    }
  }

  static bool Read(const Message* aMsg, PickleIterator* aIter, paramType* aResult)
  {
    uint32_t count;
    if (!ReadParam(aMsg, aIter, &count)) {
      return false;
    }
    while (count > 0) {
      typename mozilla::RemoveConst<typename mozilla::RemoveReference<typename KeyClass::KeyType>::Type>::Type key;
      DataType data;
      if (!ReadParam(aMsg, aIter, &key) &&
          !ReadParam(aMsg, aIter, &data)) {
        return false;
      }
      aResult->Put(key, std::move(data));
    }
    return true;
  }
};

template<>
struct ParamTraits<mozilla::layers::SharedSurfacesMemoryReport::SurfaceEntry>
  : public PlainOldDataSerializer<mozilla::layers::SharedSurfacesMemoryReport::SurfaceEntry>
{
};

template<>
struct ParamTraits<mozilla::layers::SharedSurfacesMemoryReport>
{
  typedef mozilla::layers::SharedSurfacesMemoryReport paramType;

  static void Write(Message* aMsg, const paramType& aParam)
  {
    WriteParam(aMsg, aParam.mSurfaces);
  }

  static bool Read(const Message* aMsg, PickleIterator* aIter, paramType* aResult)
  {
    return ReadParam(aMsg, aIter, &aResult->mSurfaces);
  }
};

} // namespace IPC

#endif
