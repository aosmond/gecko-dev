/* -*- Mode: C++; tab-width: 8; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* vim: set ts=8 sts=2 et sw=2 tw=80: */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "gtest/gtest.h"

#include "mozilla/gfx/2D.h"
#include "skia/include/core/SkColorPriv.h" // for SkPMSrcOver
#include "Common.h"
#include "Decoder.h"
#include "DecoderFactory.h"
#include "SourceBuffer.h"
#include "SurfaceFilters.h"
#include "SurfacePipe.h"

using namespace mozilla;
using namespace mozilla::gfx;
using namespace mozilla::image;

template <typename Func> void
WithBlendAnimationFilter(RawAccessFrameRef& aNextFrame,
                         const IntRect& aFrameRect,
                         const IntSize& aOutputSize,
                         BlendMethod aBlendMethod,
                         DisposalMethod aDisposalMethod,
                         imgFrame* aCurrentFrame,
                         imgFrame* aRestoreFrame,
                         Func aFunc)
{
  RefPtr<Decoder> decoder = CreateTrivialDecoder();
  ASSERT_TRUE(decoder != nullptr);

  BlendAnimationConfig blendAnim { aFrameRect, aBlendMethod,
                                   aCurrentFrame, aRestoreFrame };
  SurfaceConfig surfaceSink { decoder, 0, aOutputSize,
                              SurfaceFormat::B8G8R8A8, false };

  auto func = [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    aFunc(aDecoder, aFilter);
  };

  WithFilterPipeline(decoder, func, false, blendAnim, surfaceSink);

  aNextFrame = decoder->GetCurrentFrameRef();
  if (aNextFrame) {
    aNextFrame->Finish(Opacity::SOME_TRANSPARENCY,
                       aDisposalMethod,
                       FrameTimeout::FromRawMilliseconds(0),
                       aBlendMethod,
                       Some(aFrameRect));
  }
}

void
AssertConfiguringBlendAnimationFilterFails(const IntRect& aFrameRect,
                                           const IntSize& aOutputSize)
{
  RefPtr<Decoder> decoder = CreateTrivialDecoder();
  ASSERT_TRUE(decoder != nullptr);

  AssertConfiguringPipelineFails(decoder,
                                 BlendAnimationConfig { aFrameRect,
                                                        BlendMethod::OVER,
                                                        nullptr,
                                                        nullptr },
                                 SurfaceConfig { decoder, 0, aOutputSize,
                                                 SurfaceFormat::B8G8R8A8, false });
}

TEST(ImageBlendAnimationFilter, BlendFailsForNegativeFrameRect)
{
  // A negative frame rect size is disallowed.
  AssertConfiguringBlendAnimationFilterFails(IntRect(IntPoint(0, 0), IntSize(-1, -1)),
                                             IntSize(100, 100));
}

TEST(ImageBlendAnimationFilter, WriteFullFirstFrame)
{
  RawAccessFrameRef frame0;
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    CheckWritePixels(aDecoder, aFilter, Some(IntRect(0, 0, 100, 100)));
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });
}

TEST(ImageBlendAnimationFilter, WritePartialFirstFrame)
{
  RawAccessFrameRef frame0;
  WithBlendAnimationFilter(frame0, IntRect(25, 50, 50, 25), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    CheckWritePixels(aDecoder, aFilter, Some(IntRect(0, 0, 100, 100)),
                                        Nothing(),
                                        Some(IntRect(25, 50, 50, 25)),
                                        Some(IntRect(25, 50, 50, 25)));
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });
}

static void
TestWithBlendAnimationFilterClear(BlendMethod aBlendMethod)
{
  RawAccessFrameRef frame0;
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(BGRAColor::Green().AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  WithBlendAnimationFilter(frame1, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::CLEAR,
                           frame0.get(), nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(BGRAColor::Red().AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 40, 100, 20), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame1.get() != nullptr);

  RefPtr<SourceSurface> surface = frame1->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, BGRAColor::Green()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, BGRAColor::Red()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, BGRAColor::Green()));

  RawAccessFrameRef frame2;
  WithBlendAnimationFilter(frame2, IntRect(0, 50, 100, 20), IntSize(100, 100),
                           aBlendMethod, DisposalMethod::KEEP,
                           frame1.get(), frame0.get(),
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(BGRAColor::Blue().AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
  });

  ASSERT_TRUE(frame2.get() != nullptr);

  surface = frame2->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, BGRAColor::Green()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 10, BGRAColor::Transparent()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 50, 20, BGRAColor::Blue()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 70, 30, BGRAColor::Green()));
}

TEST(ImageBlendAnimationFilter, ClearWithOver)
{
  TestWithBlendAnimationFilterClear(BlendMethod::OVER);
}

TEST(ImageBlendAnimationFilter, ClearWithSource)
{
  TestWithBlendAnimationFilterClear(BlendMethod::SOURCE);
}

TEST(ImageBlendAnimationFilter, KeepWithSource)
{
  RawAccessFrameRef frame0;
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(BGRAColor::Green().AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  WithBlendAnimationFilter(frame1, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           frame0.get(), nullptr,
                           [](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(BGRAColor::Red().AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 40, 100, 20), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame1.get() != nullptr);

  RefPtr<SourceSurface> surface = frame1->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, BGRAColor::Green()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, BGRAColor::Red()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, BGRAColor::Green()));
}

TEST(ImageBlendAnimationFilter, KeepWithOver)
{
  RawAccessFrameRef frame0;
  BGRAColor frameColor0(0, 0xFF, 0, 0x40);
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor0.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  BGRAColor frameColor1(0, 0, 0xFF, 0x80);
  WithBlendAnimationFilter(frame1, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::OVER, DisposalMethod::KEEP,
                           frame0.get(), nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor1.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 40, 100, 20), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame1.get() != nullptr);

  BGRAColor blendedColor(0, 0x20, 0x80, 0xA0, true); // already premultiplied
  EXPECT_EQ(SkPMSrcOver(frameColor1.AsPixel(), frameColor0.AsPixel()),
            blendedColor.AsPixel());

  RefPtr<SourceSurface> surface = frame1->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, frameColor0));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, blendedColor));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, frameColor0));
}

TEST(ImageBlendAnimationFilter, RestorePreviousWithOver)
{
  RawAccessFrameRef frame0;
  BGRAColor frameColor0(0, 0xFF, 0, 0x40);
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor0.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  BGRAColor frameColor1 = BGRAColor::Green();
  WithBlendAnimationFilter(frame1, IntRect(0, 10, 100, 80), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::RESTORE_PREVIOUS,
                           frame0.get(), nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor1.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 10, 100, 80), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame2;
  BGRAColor frameColor2(0, 0, 0xFF, 0x80);
  WithBlendAnimationFilter(frame2, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::OVER, DisposalMethod::KEEP,
                           frame1.get(), frame0.get(),
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor2.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame2.get() != nullptr);

  BGRAColor blendedColor(0, 0x20, 0x80, 0xA0, true); // already premultiplied
  EXPECT_EQ(SkPMSrcOver(frameColor2.AsPixel(), frameColor0.AsPixel()),
            blendedColor.AsPixel());

  RefPtr<SourceSurface> surface = frame2->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, frameColor0));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, blendedColor));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, frameColor0));
}

TEST(ImageBlendAnimationFilter, RestorePreviousWithSource)
{
  RawAccessFrameRef frame0;
  BGRAColor frameColor0(0, 0xFF, 0, 0x40);
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor0.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  BGRAColor frameColor1 = BGRAColor::Green();
  WithBlendAnimationFilter(frame1, IntRect(0, 10, 100, 80), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::RESTORE_PREVIOUS,
                           frame0.get(), nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor1.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 10, 100, 80), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame2;
  BGRAColor frameColor2(0, 0, 0xFF, 0x80);
  WithBlendAnimationFilter(frame2, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           frame1.get(), frame0.get(),
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor2.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame2.get() != nullptr);

  RefPtr<SourceSurface> surface = frame2->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 40, frameColor0));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, frameColor2));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, frameColor0));
}

TEST(ImageBlendAnimationFilter, RestorePreviousClearWithSource)
{
  RawAccessFrameRef frame0;
  BGRAColor frameColor0 = BGRAColor::Red();
  WithBlendAnimationFilter(frame0, IntRect(0, 0, 100, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor0.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame1;
  BGRAColor frameColor1 = BGRAColor::Blue();
  WithBlendAnimationFilter(frame1, IntRect(0, 0, 100, 20), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::CLEAR,
                           frame0.get(), nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor1.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 20), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame2;
  BGRAColor frameColor2 = BGRAColor::Green();
  WithBlendAnimationFilter(frame2, IntRect(0, 10, 100, 80), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::RESTORE_PREVIOUS,
                           frame1.get(), nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor2.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 90), aFilter->DirtyRect());
  });

  RawAccessFrameRef frame3;
  BGRAColor frameColor3 = BGRAColor::Blue();
  WithBlendAnimationFilter(frame3, IntRect(0, 40, 100, 20), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           frame2.get(), frame1.get(),
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor3.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  ASSERT_TRUE(frame3.get() != nullptr);

  RefPtr<SourceSurface> surface = frame3->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 20, BGRAColor::Transparent()));
  EXPECT_TRUE(RowsAreSolidColor(surface, 20, 20, frameColor0));
  EXPECT_TRUE(RowsAreSolidColor(surface, 40, 20, frameColor3));
  EXPECT_TRUE(RowsAreSolidColor(surface, 60, 40, frameColor0));
}

TEST(ImageBlendAnimationFilter, PartialOverlapFrameRect)
{
  RawAccessFrameRef frame0;
  BGRAColor frameColor0 = BGRAColor::Red();
  WithBlendAnimationFilter(frame0, IntRect(-10, -20, 110, 100), IntSize(100, 100),
                           BlendMethod::SOURCE, DisposalMethod::KEEP,
                           nullptr, nullptr,
                           [&](Decoder* aDecoder, SurfaceFilter* aFilter) {
    auto result = aFilter->WritePixels<uint32_t>([&] {
      return AsVariant(frameColor0.AsPixel());
    });
    EXPECT_EQ(WriteState::FINISHED, result);
    EXPECT_EQ(IntRect(0, 0, 100, 100), aFilter->DirtyRect());
  });

  RefPtr<SourceSurface> surface = frame0->GetSourceSurface();
  EXPECT_TRUE(RowsAreSolidColor(surface, 0, 80, frameColor0));
  EXPECT_TRUE(RowsAreSolidColor(surface, 80, 20, BGRAColor::Transparent()));
}

