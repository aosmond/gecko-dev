/* -*- Mode: C++; tab-width: 20; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "EpochScheduler.h"

namespace mozilla {
namespace layers {

EpochScheduler::~EpochScheduler()
{
}

void
EpochScheduler::Dispatch(RefPtr<EpochRunnable>&& aEvent)
{
  MOZ_ASSERT_IF(!mEvents.empty(),
                mEvents.back()->GetEpoch() <= aEvent->GetEpoch());
  mEvents.push(aEvent);
}

bool
EpochScheduler::Advance(const wr::Epoch& aEpoch)
{
  while (!mEvents.empty() && mEvents.front()->GetEpoch() <= aEpoch) {
    mEvents.front()->Run();
    mEvents.pop();
  }

  return mEvents.empty() && mShutdownEpoch.isSome() &&
                            *mShutdownEpoch <= aEpoch;
}

void
EpochScheduler::Shutdown(const wr::Epoch& aEpoch)
{
  mShutdownEpoch.emplace(aEpoch);
}

void
EpochScheduler::CancelShutdown()
{
  mShutdownEpoch.reset();
}

EpochSchedulerManager::~EpochSchedulerManager()
{
}

already_AddRefed<EpochScheduler>
EpochSchedulerManager::Create(const wr::PipelineId& aPipelineId)
{
  RefPtr<EpochScheduler>& scheduler =
    mSchedulers.GetOrInsert(wr::AsUint64(aPipelineId));
  if (scheduler) {
    scheduler->CancelShutdown();
  } else {
    scheduler = new EpochScheduler();
  }
  RefPtr<EpochScheduler> rv(scheduler);
  return rv.forget();
}

EpochScheduler*
EpochSchedulerManager::Get(const wr::PipelineId& aPipelineId)
{
  return mSchedulers.GetWeak(wr::AsUint64(aPipelineId));
}

void
EpochSchedulerManager::Advance(const wr::PipelineId& aPipelineId, const wr::Epoch& aEpoch)
{
  EpochScheduler* scheduler = mSchedulers.GetWeak(wr::AsUint64(aPipelineId));
  if (!scheduler) {
    MOZ_ASSERT_UNREACHABLE("Invalid pipeline ID!");
    return;
  }
  scheduler->Advance(aEpoch);
}

} // layers
} // mozilla
