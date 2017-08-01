/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*-
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/Move.h"
#include "AnimationFrameBuffer.h"

using namespace mozilla;
using namespace mozilla::image;

static RawAccessFrameRef
CreateEmptyFrame()
{
  RefPtr<imgFrame> frame = new imgFrame();
  nsresult rv = frame->InitForAnimator(nsIntSize(1, 1), SurfaceFormat::B8G8R8A8);
  EXPECT_TRUE(NS_SUCCEEDED(rv));
  RawAccessFrameRef frameRef = frame->RawAccessRef();
  frame->Finish();
  return frameRef;
}

static bool
Fill(AnimationFrameBuffer& buffer, size_t aLength, bool aComplete, RawAccessFrameRef* aFrame = nullptr)
{
  bool keepDecoding = false;
  RawAccessFrameRef frame;

  for (size_t i = 0; i < aLength; ++i) {
    frame = CreateEmptyFrame();
    keepDecoding = buffer.Insert(Move(frame->RawAccessRef()));
  }

  if (aComplete) {
    keepDecoding = buffer.MarkComplete();
  }

  if (aFrame) {
    *aFrame = Move(frame);
  }
  return keepDecoding;
}

static void
CheckFrames(const AnimationFrameBuffer& buffer, size_t aStart, size_t aEnd, bool aExpected)
{
  for (size_t i = aStart; i < aEnd; ++i) {
    EXPECT_EQ(aExpected, !!buffer.Frames()[i]);
  }
}

static void
CheckRemoved(const AnimationFrameBuffer& buffer, size_t aStart, size_t aEnd)
{
  CheckFrames(buffer, aStart, aEnd, false);
}

static void
CheckRetained(const AnimationFrameBuffer& buffer, size_t aStart, size_t aEnd)
{
  CheckFrames(buffer, aStart, aEnd, true);
}

class ImageAnimationFrameBuffer : public ::testing::Test
{
public:
  ImageAnimationFrameBuffer()
  { }
};

TEST_F(ImageAnimationFrameBuffer, InitialState)
{
  const size_t kThreshold = 800;
  const size_t kBatch = 100;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);

  EXPECT_EQ(kThreshold, buffer.Threshold());
  EXPECT_EQ(kBatch, buffer.Batch());
  EXPECT_EQ(size_t(0), buffer.Displayed());
  EXPECT_EQ(kBatch * 2, buffer.Pending());
  EXPECT_FALSE(buffer.MayDiscard());
  EXPECT_FALSE(buffer.Complete());
  EXPECT_TRUE(buffer.Frames().IsEmpty());
}

TEST_F(ImageAnimationFrameBuffer, ThresholdTooSmall)
{
  const size_t kThreshold = 0;
  const size_t kBatch = 10;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);

  EXPECT_EQ(kBatch * 2 + 2, buffer.Threshold());
  EXPECT_EQ(kBatch, buffer.Batch());
  EXPECT_EQ(kBatch * 2, buffer.Pending());
}

TEST_F(ImageAnimationFrameBuffer, BatchTooSmall)
{
  const size_t kThreshold = 10;
  const size_t kBatch = 0;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);

  EXPECT_EQ(kThreshold, buffer.Threshold());
  EXPECT_EQ(size_t(1), buffer.Batch());
  EXPECT_EQ(size_t(2), buffer.Pending());
}

TEST_F(ImageAnimationFrameBuffer, BatchTooBig)
{
  const size_t kThreshold = 50;
  const size_t kBatch = SIZE_MAX;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);

  // The rounding is important here (e.g. SIZE_MAX/4 * 2 != SIZE_MAX/2).
  EXPECT_EQ(SIZE_MAX/4, buffer.Batch());
  EXPECT_EQ(buffer.Batch() * 2 + 2, buffer.Threshold());
  EXPECT_EQ(buffer.Batch() * 2, buffer.Pending());
}

TEST_F(ImageAnimationFrameBuffer, FinishUnderBatchAndThreshold)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 10;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);
  const auto& frames = buffer.Frames();

  EXPECT_EQ(kBatch * 2, buffer.Pending());

  RawAccessFrameRef firstFrame;
  for (size_t i = 0; i < 5; ++i) {
    RawAccessFrameRef frame = CreateEmptyFrame();
    bool keepDecoding = buffer.Insert(Move(frame->RawAccessRef()));
    EXPECT_TRUE(keepDecoding);
    EXPECT_FALSE(buffer.Complete());

    if (i == 4) {
      keepDecoding = buffer.MarkComplete();
      EXPECT_FALSE(keepDecoding);
      EXPECT_TRUE(buffer.Complete());
      EXPECT_EQ(size_t(0), buffer.Pending());
    }

    EXPECT_FALSE(buffer.MayDiscard());

    DrawableFrameRef gotFrame = buffer.Get(i);
    EXPECT_EQ(frame.get(), gotFrame.get());
    ASSERT_EQ(i + 1, frames.Length());
    EXPECT_EQ(frame.get(), frames[i].get());

    bool restartDecoder = buffer.Advance();
    EXPECT_FALSE(restartDecoder);
    EXPECT_EQ(i, buffer.Displayed());

    if (i == 0) {
      firstFrame = Move(frame);
    }

    gotFrame = buffer.Get(0);
    EXPECT_EQ(firstFrame.get(), gotFrame.get());
  }

  // Loop again over the animation and make sure it is still all there.
  for (size_t i = 0; i < frames.Length(); ++i) {
    DrawableFrameRef gotFrame = buffer.Get(i);
    EXPECT_TRUE(gotFrame);

    bool restartDecoder = buffer.Advance();
    EXPECT_FALSE(restartDecoder);
  }
}

TEST_F(ImageAnimationFrameBuffer, FinishMultipleBatchesUnderThreshold)
{
  const size_t kThreshold = 30;
  const size_t kBatch = 2;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);
  const auto& frames = buffer.Frames();

  EXPECT_EQ(kBatch * 2, buffer.Pending());

  // Add frames until it tells us to stop.
  bool keepDecoding;
  do {
    keepDecoding = Fill(buffer, 1, false);
    EXPECT_FALSE(buffer.Complete());
    EXPECT_FALSE(buffer.MayDiscard());
  } while (keepDecoding);

  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(4), frames.Length());

  // Progress through the animation until it lets us decode again.
  bool restartDecoder = true;
  size_t i = 0;
  do {
    DrawableFrameRef gotFrame = buffer.Get(i++);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
  } while (!restartDecoder);

  EXPECT_EQ(size_t(2), buffer.Pending());
  EXPECT_EQ(size_t(2), buffer.Displayed());

  // Add the last frame.
  keepDecoding = Fill(buffer, 1, true);
  EXPECT_FALSE(keepDecoding);
  EXPECT_TRUE(buffer.Complete());
  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(5), frames.Length());

  // Finish progressing through the animation.
  for ( ; i < frames.Length(); ++i) {
    DrawableFrameRef gotFrame = buffer.Get(i);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
    EXPECT_FALSE(restartDecoder);
  }

  // Loop again over the animation and make sure it is still all there.
  for (i = 0; i < frames.Length(); ++i) {
    DrawableFrameRef gotFrame = buffer.Get(i);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
    EXPECT_FALSE(restartDecoder);
  }

  // Loop to the third frame and then reset the animation.
  for (i = 0; i < 3; ++i) {
    DrawableFrameRef gotFrame = buffer.Get(i);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
    EXPECT_FALSE(restartDecoder);
  }

  // Since we are below the threshold, we can reset the get index only.
  // Nothing else should have changed.
  restartDecoder = buffer.Reset(/* aAdvanceOnly */ true);
  EXPECT_FALSE(restartDecoder);
  CheckRetained(buffer, 0, 5);
  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(0), buffer.Displayed());
}

TEST_F(ImageAnimationFrameBuffer, MayDiscard)
{
  const size_t kThreshold = 8;
  const size_t kBatch = 3;
  AnimationFrameBuffer buffer;
  buffer.Init(kThreshold, kBatch);
  const auto& frames = buffer.Frames();

  EXPECT_EQ(kBatch * 2, buffer.Pending());

  // Add frames until it tells us to stop.
  bool keepDecoding;
  do {
    keepDecoding = Fill(buffer, 1, false);
    EXPECT_FALSE(buffer.Complete());
    EXPECT_FALSE(buffer.MayDiscard());
  } while (keepDecoding);

  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(6), frames.Length());

  // Progress through the animation until it lets us decode again.
  bool restartDecoder;
  size_t i = 0;
  do {
    DrawableFrameRef gotFrame = buffer.Get(i++);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
  } while (!restartDecoder);

  EXPECT_EQ(size_t(3), buffer.Pending());
  EXPECT_EQ(size_t(3), buffer.Displayed());

  // Add more frames.
  do {
    keepDecoding = Fill(buffer, 1, false);
    EXPECT_FALSE(buffer.Complete());
  } while (keepDecoding);

  EXPECT_TRUE(buffer.MayDiscard());
  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(9), frames.Length());

  // It should have be able to remove one frame given we have advanced to the
  // fourth frame.
  CheckRetained(buffer, 0, 1);
  CheckRemoved(buffer, 1, 2);
  CheckRetained(buffer, 2, 9);

  // Progress through the animation so more. Make sure it removes frames as we
  // go along.
  do {
    DrawableFrameRef gotFrame = buffer.Get(i++);
    EXPECT_TRUE(gotFrame);
    restartDecoder = buffer.Advance();
    EXPECT_FALSE(frames[i - 2]);
    EXPECT_TRUE(frames[i - 1]);
  } while (!restartDecoder);

  EXPECT_EQ(size_t(3), buffer.Pending());
  EXPECT_EQ(size_t(6), buffer.Displayed());

  // Add the last frame. It should still let us add more frames, but the next
  // frame will restart at the beginning.
  keepDecoding = Fill(buffer, 1, true);
  EXPECT_TRUE(keepDecoding);
  EXPECT_TRUE(buffer.Complete());
  EXPECT_EQ(size_t(2), buffer.Pending());
  EXPECT_EQ(size_t(10), frames.Length());

  // Use remaining pending room. It shouldn't add new frames, only replace.
  do {
    keepDecoding = Fill(buffer, 1, false);
  } while (keepDecoding);

  EXPECT_EQ(size_t(0), buffer.Pending());
  EXPECT_EQ(size_t(10), frames.Length());

  // Advance as far as we can. This should require us to loop the animation to
  // reach a missing frame.
  do {
    if (i == frames.Length()) {
      i = 0;
    }

    DrawableFrameRef gotFrame = buffer.Get(i);
    if (!gotFrame) {
      break;
    }

    ++i;
    restartDecoder = buffer.Advance();
  } while (true);

  EXPECT_EQ(size_t(3), buffer.Pending());
  EXPECT_EQ(size_t(2), i);
  EXPECT_EQ(size_t(1), buffer.Displayed());

  // Decode some more.
  keepDecoding = Fill(buffer, buffer.Pending(), false);
  EXPECT_FALSE(keepDecoding);
  EXPECT_EQ(size_t(0), buffer.Pending());

  // Can we retry advancing again?
  DrawableFrameRef gotFrame = buffer.Get(i);
  EXPECT_TRUE(gotFrame);
  restartDecoder = buffer.Advance();
  EXPECT_EQ(size_t(2), buffer.Displayed());
  EXPECT_TRUE(frames[i - 2]); // first frame
  EXPECT_TRUE(frames[i - 1]);
  ++i;

  // One more frame, and we can start clearing previous-previous frames again.
  gotFrame = buffer.Get(i);
  EXPECT_TRUE(gotFrame);
  restartDecoder = buffer.Advance();
  EXPECT_EQ(size_t(3), buffer.Displayed());
  EXPECT_FALSE(frames[i - 2]);
  EXPECT_TRUE(frames[i - 1]);
  ++i;
}

