/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef mozilla_image_AnimationFrameBuffer_h
#define mozilla_image_AnimationFrameBuffer_h

#include "ISurfaceProvider.h"

namespace mozilla {
namespace image {

/**
 * An AnimationFrameBuffer owns the frames outputted by an animated image
 * decoder as well as directing its owner on how to drive the decoder,
 * whether to produce more or to stop.
 *
 * Based upon its given configuration parameters, it will retain up to a
 * certain number of frames in the buffer before deciding to discard previous
 * frames, and relying upon the decoder to recreate older frames when the
 * animation loops. It will also request that the decoder stop producing more
 * frames when the display of the frames are far behind -- this allows other
 * tasks and images which require decoding to take execution priority.
 *
 * The desire is that smaller animated images should be kept completely in
 * memory while larger animated images should only keep a certain number of
 * frames to minimize our memory footprint at the cost of CPU.
 */
class AnimationFrameBuffer final
{
public:
  AnimationFrameBuffer();

  /**
   * Configure the frame buffer with a particular threshold and batch size. Note
   * that the frame buffer may adjust the given values.
   *
   * @param aThreshold Maximum number of frames that may be stored in the frame
   *                   buffer before it may discard already displayed frames.
   *                   Once exceeded, it will discard the previous frame to the
   *                   current frame whenever Advance is called. It always
   *                   retains the first frame.
   *
   * @param aBatch     Number of frames we request to be decoded each time it
   *                   decides we need more.
   */
  void Init(size_t aThreshold, size_t aBatch);

  /**
   * Access a specific frame from the frame buffer. May return nothing.
   */
  DrawableFrameRef Get(size_t aFrame);

  /**
   * Inserts a frame into the frame buffer. If it has yet to fully decode the
   * animated image yet, then it will append the frame to its internal buffer.
   * If it has been fully decoded, it will replace the next frame in its buffer
   * with the given frame.
   *
   * Once we have a sufficient number of frames buffered relative to the
   * currently displayed frame, it will return false to indicate the caller
   * should stop decoding.
   *
   * @returns True if the decoder should decode another frame.
   */
  bool Insert(RawAccessFrameRef&& aFrame);

  /**
   * This should be called after the last frame has been inserted. If the buffer
   * is discarding old frames, it may request more frames to be decoded. In this
   * case that means the decoder should start again from the beginning. This
   * return value should be used in preference to that of the Insert call.
   *
   * @returns True if the decoder should decode another frame.
   */
  bool MarkComplete();

  /**
   * Try to advance the currently displayed frame of the frame buffer. If it
   * reaches the end, it will loop back to the beginning. If it reaches an
   * undecoded frame (e.g. because the decoder is behind), then it will not
   * advance.
   *
   * As we advance, the number of frames we have buffered ahead of the current
   * will shrink. Once that becomes too few, we will request a batch-sized set
   * of frames to be decoded from the decoder.
   *
   * @returns True if the caller should restart the decoder.
   */
  bool Advance();
  bool Advance(size_t aUpToFrame);
  bool AdvanceBy(size_t aFrames);

  /**
   * Resets the currently displayed frame of the frame buffer to the beginning.
   * If the buffer is discarding old frames, it will actually discard all frames
   * besides the first.
   *
   * @param aAdvanceOnly Must be false if it is discarding frames, true if it
   *                     retains them.
   *
   * @returns True if the caller should restart the decoder.
   */
  bool Reset(bool aAdvanceOnly);

  bool MayDiscard() const { return mFrames.Length() > mThreshold; }
  bool Complete() const { return mSizeKnown; }
  size_t Displayed() const { return mGetIndex; }
  size_t Pending() const { return mPending; }
  size_t Batch() const { return mBatch; }
  size_t Threshold() const { return mThreshold; }
  const nsTArray<RawAccessFrameRef>& Frames() const { return mFrames; }

private:
  /// The frames of this animation, in order.
  nsTArray<RawAccessFrameRef> mFrames;

  size_t mThreshold;
  size_t mBatch;
  size_t mPending;
  size_t mAdvance;
  size_t mInsertIndex;
  size_t mGetIndex;
  bool mSizeKnown;
};

} // namespace image
} // namespace mozilla

#endif // mozilla_image_AnimationFrameBuffer_h
