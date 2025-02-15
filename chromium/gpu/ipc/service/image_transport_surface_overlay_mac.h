// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_
#define GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_

#include <vector>

#import "base/mac/scoped_nsobject.h"
#include "base/memory/weak_ptr.h"
#include "components/viz/common/gpu/gpu_vsync_callback.h"
#include "gpu/ipc/service/command_buffer_stub.h"
#include "gpu/ipc/service/image_transport_surface.h"
#include "ui/gfx/ca_layer_result.h"
#include "ui/gfx/presentation_feedback.h"
#include "ui/gl/gl_bindings.h"
#include "ui/gl/gl_surface.h"
#include "ui/gl/presenter.h"

// Put gpu_vsync_mac.h (which includes ui/display/mac/display_link_mac.h)
// after ui/gl/gl_xxx.h. There is a conflict between MacOSX sdk gltypes.h and
// third_party/mesa_headers/GL/glext.h
#if BUILDFLAG(IS_MAC)
#include "gpu/ipc/service/gpu_vsync_mac.h"
#endif

@class CAContext;
@class CALayer;

namespace ui {
class CALayerTreeCoordinator;
struct CARendererLayerParams;
}

namespace gl {
class GLFence;
}

namespace gpu {

class ImageTransportSurfaceOverlayMacEGL : public gl::Presenter {
 public:
  ImageTransportSurfaceOverlayMacEGL(
      base::WeakPtr<ImageTransportSurfaceDelegate> delegate);

  // Presenter implementation
  bool Resize(const gfx::Size& size,
              float scale_factor,
              const gfx::ColorSpace& color_space,
              bool has_alpha) override;
  void Present(SwapCompletionCallback completion_callback,
               PresentationCallback presentation_callback,
               gfx::FrameData data) override;

  bool ScheduleOverlayPlane(
      gl::OverlayImage image,
      std::unique_ptr<gfx::GpuFence> gpu_fence,
      const gfx::OverlayPlaneData& overlay_plane_data) override;
  bool ScheduleCALayer(const ui::CARendererLayerParams& params) override;

  void SetCALayerErrorCode(gfx::CALayerResult ca_layer_error_code) override;

  // GLSurface override
#if BUILDFLAG(IS_MAC)
  bool SupportsGpuVSync() const override;
  void SetVSyncDisplayID(int64_t display_id) override;
  void SetGpuVSyncEnabled(bool enabled) override;
#endif

 private:
  ~ImageTransportSurfaceOverlayMacEGL() override;

  gfx::SwapResult SwapBuffersInternal(
      gl::GLSurface::SwapCompletionCallback completion_callback,
      gl::GLSurface::PresentationCallback presentation_callback);
  void ApplyBackpressure();
  void BufferPresented(gl::GLSurface::PresentationCallback callback,
                       const gfx::PresentationFeedback& feedback);

  base::WeakPtr<ImageTransportSurfaceDelegate> delegate_;

  const bool use_remote_layer_api_;
  base::scoped_nsobject<CAContext> ca_context_;
  std::unique_ptr<ui::CALayerTreeCoordinator> ca_layer_tree_coordinator_;

  gfx::Size pixel_size_;
  float scale_factor_;
  gfx::CALayerResult ca_layer_error_code_ = gfx::kCALayerSuccess;

  // A GLFence marking the end of the previous frame, used for applying
  // backpressure.
  uint64_t previous_frame_fence_ = 0;

#if BUILDFLAG(IS_MAC)
  std::unique_ptr<GpuVSyncMac> gpu_vsync_mac_;
#endif

  base::WeakPtrFactory<ImageTransportSurfaceOverlayMacEGL> weak_ptr_factory_;
};

}  // namespace gpu

#endif  // GPU_IPC_SERVICE_IMAGE_TRANSPORT_SURFACE_OVERLAY_MAC_H_
