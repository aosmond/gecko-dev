/* -*- Mode: C++; tab-width: 2; indent-tabs-mode: nil; c-basic-offset: 2 -*- */
/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "RendererOGL.h"
#include "GLContext.h"
#include "GLContextProvider.h"
#include "mozilla/gfx/Logging.h"
#include "mozilla/layers/CompositorBridgeParent.h"
#include "mozilla/layers/CompositorThread.h"
#include "mozilla/layers/SharedSurfaceBridgeParent.h"
#include "mozilla/webrender/RenderBufferTextureHost.h"
#include "mozilla/webrender/RenderTextureHostOGL.h"
#include "mozilla/widget/CompositorWidget.h"

namespace mozilla {
namespace wr {

WrExternalImage LockExternalImage(void* aObj, WrExternalImageId aId, uint8_t aChannelIndex)
{
  RendererOGL* renderer = reinterpret_cast<RendererOGL*>(aObj);

  RefPtr<gfx::DataSourceSurface> surface =
    layers::SharedSurfaceBridgeParent::Get(aId);
  if (surface) {
    gfx::DataSourceSurface::MappedSurface mapping;
    DebugOnly<bool> rv = surface->Map(gfx::DataSourceSurface::MapType::READ,
                                      &mapping);
    MOZ_ASSERT(rv);

    gfx::IntSize size = surface->GetSize();
    size_t len = mapping.mStride * size.height;

    // Keep a reference active to the surface, in case it gets removed by the
    // owner while we are rendering.
    Unused << surface.forget().take();
    return RawDataToWrExternalImage(mapping.mData, len);
  }

  RenderTextureHost* texture = renderer->GetRenderTexture(aId);
  MOZ_ASSERT(texture, "Image missing from renderer and shared surface cache!");

  if (texture->AsBufferTextureHost()) {
    RenderBufferTextureHost* bufferTexture = texture->AsBufferTextureHost();
    MOZ_ASSERT(bufferTexture);
    bufferTexture->Lock();
    RenderBufferTextureHost::RenderBufferData data =
        bufferTexture->GetBufferDataForRender(aChannelIndex);

    return RawDataToWrExternalImage(data.mData, data.mBufferSize);
  } else {
    // texture handle case
    RenderTextureHostOGL* textureOGL = texture->AsTextureHostOGL();
    MOZ_ASSERT(textureOGL);

    textureOGL->SetGLContext(renderer->mGL);
    textureOGL->Lock();
    gfx::IntSize size = textureOGL->GetSize(aChannelIndex);

    return NativeTextureToWrExternalImage(textureOGL->GetGLHandle(aChannelIndex),
                                          0, 0,
                                          size.width, size.height);
  }
}

void UnlockExternalImage(void* aObj, WrExternalImageId aId, uint8_t aChannelIndex)
{
  RendererOGL* renderer = reinterpret_cast<RendererOGL*>(aObj);

  RefPtr<gfx::DataSourceSurface> surface =
    layers::SharedSurfaceBridgeParent::Get(aId);
  if (surface) {
    // Unmap and release the secondary reference to the surface that we use to
    // ensure it was kept alive while rendering (Map only promises the data
    // is available, not that the surface will remain).
    surface->Unmap();
    surface.get()->Release();
    return;
  }

  RenderTextureHost* texture = renderer->GetRenderTexture(aId);
  MOZ_ASSERT(texture, "Image missing from renderer and shared surface cache!");
  texture->Unlock();
}

RendererOGL::RendererOGL(RefPtr<RenderThread>&& aThread,
                         RefPtr<gl::GLContext>&& aGL,
                         RefPtr<widget::CompositorWidget>&& aWidget,
                         wr::WindowId aWindowId,
                         WrRenderer* aWrRenderer,
                         layers::CompositorBridgeParentBase* aBridge)
  : mThread(aThread)
  , mGL(aGL)
  , mWidget(aWidget)
  , mWrRenderer(aWrRenderer)
  , mBridge(aBridge)
  , mWindowId(aWindowId)
{
  MOZ_ASSERT(mThread);
  MOZ_ASSERT(mGL);
  MOZ_ASSERT(mWidget);
  MOZ_ASSERT(mWrRenderer);
  MOZ_ASSERT(mBridge);
  MOZ_COUNT_CTOR(RendererOGL);
}

RendererOGL::~RendererOGL()
{
  MOZ_COUNT_DTOR(RendererOGL);
  if (!mGL->MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current during destroying.";
    // Leak resources!
    return;
  }
  wr_renderer_delete(mWrRenderer);
}

WrExternalImageHandler
RendererOGL::GetExternalImageHandler()
{
  return WrExternalImageHandler {
    this,
    LockExternalImage,
    UnlockExternalImage,
  };
}

void
RendererOGL::Update()
{
  wr_renderer_update(mWrRenderer);
}

bool
RendererOGL::Render()
{
  if (!mGL->MakeCurrent()) {
    gfxCriticalNote << "Failed to make render context current, can't draw.";
    return false;
  }

  mozilla::widget::WidgetRenderingContext widgetContext;

#if defined(XP_MACOSX)
  widgetContext.mGL = mGL;
// TODO: we don't have a notion of compositor here.
//#elif defined(MOZ_WIDGET_ANDROID)
//  widgetContext.mCompositor = mCompositor;
#endif

  if (!mWidget->PreRender(&widgetContext)) {
    return false;
  }
  // XXX set clear color if MOZ_WIDGET_ANDROID is defined.
  // XXX pass the actual render bounds instead of an empty rect.
  mWidget->DrawWindowUnderlay(&widgetContext, LayoutDeviceIntRect());

  auto size = mWidget->GetClientSize();
  wr_renderer_render(mWrRenderer, size.width, size.height);

  mGL->SwapBuffers();
  mWidget->DrawWindowOverlay(&widgetContext, LayoutDeviceIntRect());
  mWidget->PostRender(&widgetContext);

  // TODO: Flush pending actions such as texture deletions/unlocks and
  //       textureHosts recycling.

  return true;
}

void
RendererOGL::Pause()
{
#ifdef MOZ_WIDGET_ANDROID
  if (!mGL || mGL->IsDestroyed()) {
    return;
  }
  // ReleaseSurface internally calls MakeCurrent.
  mGL->ReleaseSurface();
#endif
}

bool
RendererOGL::Resume()
{
#ifdef MOZ_WIDGET_ANDROID
  if (!mGL || mGL->IsDestroyed()) {
    return false;
  }
  // RenewSurface internally calls MakeCurrent.
  return mGL->RenewSurface(mWidget);
#else
  return true;
#endif
}

void
RendererOGL::SetProfilerEnabled(bool aEnabled)
{
  wr_renderer_set_profiler_enabled(mWrRenderer, aEnabled);
}

WrRenderedEpochs*
RendererOGL::FlushRenderedEpochs()
{
  return wr_renderer_flush_rendered_epochs(mWrRenderer);
}

RenderTextureHost*
RendererOGL::GetRenderTexture(WrExternalImageId aExternalImageId)
{
  return mThread->GetRenderTexture(aExternalImageId);
}

} // namespace wr
} // namespace mozilla
