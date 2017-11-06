/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef MOZILLA_LAYERS_EPOCHSCHEDULER
#define MOZILLA_LAYERS_EPOCHSCHEDULER

#include <queue>
#include "mozilla/RefPtr.h"
#include "mozilla/webrender/WebRenderTypes.h"
#include "nsRefPtrHashtable.h"

namespace mozilla {
namespace wr {

class EpochRunnable
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(EpochRunnable)

  EpochRunnable(const wr::Epoch& aEpoch)
    : mEpoch(aEpoch)
  { }

  const wr::Epoch& GetEpoch() const { return mEpoch; }

  virtual void Run() { }

protected:
  virtual ~EpochRunnable() { }

private:
  const wr::Epoch mEpoch;
};

class EpochScheduler final
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(EpochScheduler)

  EpochScheduler()
  { }

  /**
   * Add the given event to the queue to be run when its epoch is reached or
   * passed. The epoch of the event should always be equal to or later than
   * that of the tail event in the queue.
   *
   * @param aEvent Event to append to the queue.
   */
  void Dispatch(RefPtr<EpochRunnable>&& aEvent);

  void Dispatch(EpochRunnable* aEvent)
  {
    RefPtr<EpochRunnable> event(aEvent);
    Dispatch(Move(event));
  }

  /**
   * Execute any pending runnables up to the given epoch. This should be used
   * by the EpochSchedulerManager exclusively.
   *
   * @returns True if it has finished shutdown, false if still active.
   */
  bool Advance(const wr::Epoch& aEpoch);

  /**
   * Request the event target to shutdown. Once the final runnable is executed,
   * and the given epoch is observed, it will destroy itself.
   *
   * @param aEpoch The last epoch to observe.
   */
  void Shutdown(const wr::Epoch& aEpoch);

  /**
   * Abort the previously requested shutdown.
   */
  void CancelShutdown();

private:
  ~EpochScheduler();

  Maybe<wr::Epoch> mShutdownEpoch;
  std::queue<RefPtr<EpochRunnable>> mEvents;
};

class EpochSchedulerManager final
{
public:
  NS_INLINE_DECL_THREADSAFE_REFCOUNTING(EpochSchedulerManager)

  EpochSchedulerManager()
  { }

  already_AddRefed<EpochScheduler> Create(const PipelineId& aPipelineId);
  EpochScheduler* Get(const PipelineId& aPipelineId);
  void Advance(const PipelineId& aPipelineId, const wr::Epoch& aEpoch);

private:
  ~EpochSchedulerManager();

  nsRefPtrHashtable<nsUint64HashKey, EpochScheduler> mSchedulers;
};

} // wr
} // mozilla

#endif // MOZILLA_LAYERS_EPOCHSCHEDULER
