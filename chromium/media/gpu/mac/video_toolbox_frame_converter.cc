// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "media/gpu/mac/video_toolbox_frame_converter.h"

#include "base/functional/bind.h"
#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/task/bind_post_task.h"
#include "gpu/GLES2/gl2extchromium.h"
#include "gpu/command_buffer/common/mailbox_holder.h"
#include "gpu/command_buffer/common/shared_image_usage.h"
#include "gpu/command_buffer/common/sync_token.h"
#include "gpu/command_buffer/service/scheduler.h"
#include "gpu/ipc/common/gpu_memory_buffer_impl_io_surface.h"
#include "gpu/ipc/service/gpu_channel.h"
#include "gpu/ipc/service/shared_image_stub.h"
#include "media/base/media_log.h"
#include "media/base/media_switches.h"
#include "ui/gfx/color_space.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/gfx/geometry/size.h"
#include "ui/gfx/gpu_memory_buffer.h"

namespace media {

namespace {

constexpr uint32_t kSharedImageUsage =
    gpu::SHARED_IMAGE_USAGE_DISPLAY_READ | gpu::SHARED_IMAGE_USAGE_SCANOUT |
    gpu::SHARED_IMAGE_USAGE_MACOS_VIDEO_TOOLBOX |
    gpu::SHARED_IMAGE_USAGE_RASTER | gpu::SHARED_IMAGE_USAGE_GLES2;

constexpr char kSharedImageDebugLabel[] = "VideoToolboxVideoDecoder";

}  // namespace

VideoToolboxFrameConverter::VideoToolboxFrameConverter(
    scoped_refptr<base::SequencedTaskRunner> gpu_task_runner,
    std::unique_ptr<MediaLog> media_log,
    GetCommandBufferStubCB get_stub_cb)
    : base::RefCountedDeleteOnSequence<VideoToolboxFrameConverter>(
          gpu_task_runner),
      gpu_task_runner_(std::move(gpu_task_runner)),
      media_log_(std::move(media_log)),
      get_stub_cb_(std::move(get_stub_cb)) {
  DVLOG(1) << __func__;
  DCHECK(get_stub_cb_);
  DCHECK(IsMultiPlaneFormatForHardwareVideoEnabled());
}

VideoToolboxFrameConverter::~VideoToolboxFrameConverter() {
  DVLOG(1) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());
  DestroyStub();
}

void VideoToolboxFrameConverter::OnWillDestroyStub(bool have_context) {
  DVLOG(1) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());
  DestroyStub();
}

void VideoToolboxFrameConverter::Initialize() {
  DVLOG(4) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());
  DCHECK(!initialized_);

  initialized_ = true;

  stub_ = std::move(get_stub_cb_).Run();
  if (!stub_) {
    DVLOG(1) << __func__ << ": Failed to get command buffer stub.";
    MEDIA_LOG(ERROR, media_log_.get()) << "Failed to get command buffer stub.";
    return;
  }

  DCHECK(stub_->channel()->task_runner()->BelongsToCurrentThread());

  stub_->AddDestructionObserver(this);

  // kHigh priority to work around crbug.com/1035750.
  wait_sequence_id_ = stub_->channel()->scheduler()->CreateSequence(
      gpu::SchedulingPriority::kHigh, stub_->channel()->task_runner());

  sis_ = stub_->channel()->shared_image_stub();
  if (!sis_) {
    DVLOG(1) << __func__ << ": Failed to get shared image stub.";
    MEDIA_LOG(ERROR, media_log_.get()) << "Failed to get shared image stub.";
    DestroyStub();
    return;
  }

  texture_rectangle_ = stub_->decoder_context()
                           ->GetFeatureInfo()
                           ->feature_flags()
                           .arb_texture_rectangle;
}

void VideoToolboxFrameConverter::DestroyStub() {
  DVLOG(4) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());

  sis_ = nullptr;

  if (stub_) {
    stub_->channel()->scheduler()->DestroySequence(wait_sequence_id_);
    stub_->RemoveDestructionObserver(this);
    stub_ = nullptr;
  }
}

void VideoToolboxFrameConverter::Convert(
    base::ScopedCFTypeRef<CVImageBufferRef> image,
    base::TimeDelta timestamp,
    void* context,
    OutputCB output_cb) {
  DVLOG(3) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());

  if (!initialized_) {
    Initialize();
  }

  if (!stub_) {
    MEDIA_LOG(ERROR, media_log_.get()) << "Failed to get command buffer stub.";
    std::move(output_cb).Run(nullptr, context);
  }

  // TODO(crbug.com/1331597): Is this different from
  // CVImageBufferGetEncodedSize()?
  const gfx::Size coded_size(CVPixelBufferGetWidth(image),
                             CVPixelBufferGetHeight(image));
  const gfx::Rect visible_rect(CVImageBufferGetCleanRect(image));
  // TODO(crbug.com/1331597): Apply aspect ratio from configuration.
  const gfx::Size natural_size(CVImageBufferGetDisplaySize(image));
  // TODO(crbug.com/1331597): Get the color space from |image|, possibly
  // override from the configuration.
  const gfx::ColorSpace color_space = gfx::ColorSpace::CreateREC709();

  gfx::GpuMemoryBufferHandle handle;
  handle.id = gfx::GpuMemoryBufferHandle::kInvalidId;
  handle.type = gfx::GpuMemoryBufferType::IO_SURFACE_BUFFER;
  handle.io_surface.reset(CVPixelBufferGetIOSurface(image),
                          base::scoped_policy::RETAIN);

  // TODO(crbug.com/1331597): Query from |image|.
  viz::SharedImageFormat format = viz::MultiPlaneFormat::kNV12;

  gpu::Mailbox mailbox = gpu::Mailbox::GenerateForSharedImage();
  bool result = sis_->CreateSharedImage(
      mailbox, std::move(handle), format, coded_size, color_space,
      kTopLeft_GrSurfaceOrigin, kOpaque_SkAlphaType, kSharedImageUsage,
      kSharedImageDebugLabel);
  if (!result) {
    MEDIA_LOG(ERROR, media_log_.get()) << "Failed to create shared image.";
    std::move(output_cb).Run(nullptr, context);
  }

  GLenum target = texture_rectangle_ ? GL_TEXTURE_RECTANGLE_ARB : GL_TEXTURE_2D;

  gpu::MailboxHolder mailbox_holders[VideoFrame::kMaxPlanes];
  mailbox_holders[0] = gpu::MailboxHolder(mailbox, gpu::SyncToken(), target);

  // |image| must be ratained until after the release sync token passes.
  VideoFrame::ReleaseMailboxCB release_cb = base::BindPostTask(
      gpu_task_runner_,
      base::BindOnce(&VideoToolboxFrameConverter::OnVideoFrameReleased, this,
                     sis_->GetSharedImageDestructionCallback(mailbox),
                     std::move(image)));

  // It should be possible to use VideoFrame::WrapExternalGpuMemoryBuffer(),
  // which would allow the renderer to map the IOSurface, but this is more
  // expensive whenever the renderer is not doing readback.
  scoped_refptr<VideoFrame> frame = VideoFrame::WrapNativeTextures(
      PIXEL_FORMAT_NV12, mailbox_holders, std::move(release_cb), coded_size,
      visible_rect, natural_size, timestamp);

  if (!frame) {
    MEDIA_LOG(ERROR, media_log_.get()) << "Failed to create VideoFrame.";

    // |image| was dropped along with |release_cb|, but the SharedImage is still
    // alive.
    sis_->GetSharedImageDestructionCallback(mailbox).Run(gpu::SyncToken());

    std::move(output_cb).Run(nullptr, context);
    return;
  }

  // TODO(crbug.com/1331597): Apply the color space to the IOSurface, so that
  // overlay behavior is reliable.
  frame->set_color_space(color_space);
  // Note: kSharedImageFormat is only for multiplanar.
  frame->set_shared_image_format_type(
      SharedImageFormatType::kSharedImageFormat);
  // TODO(crbug.com/1331597): Does |allow_overlay| do anything on mac?
  frame->metadata().allow_overlay = true;
  // Releasing |image| must happen after command buffer commands are complete
  // (not just submitted).
  frame->metadata().read_lock_fences_enabled = true;
  // Note: only NV12 is WebGPU compatible.
  frame->metadata().is_webgpu_compatible = true;
  // TODO(crbug.com/1331597): VideoToolbox can report software usage, should
  // we plumb that through?
  frame->metadata().power_efficient = true;

  std::move(output_cb).Run(std::move(frame), context);
}

void VideoToolboxFrameConverter::OnVideoFrameReleased(
    base::OnceCallback<void(const gpu::SyncToken&)> destroy_shared_image_cb,
    base::ScopedCFTypeRef<CVImageBufferRef> image,
    const gpu::SyncToken& sync_token) {
  DVLOG(4) << __func__;
  DCHECK(gpu_task_runner_->RunsTasksInCurrentSequence());

  if (!stub_) {
    // Release |image| immediately.
    return;
  }

  // Destroy the SharedImage.
  std::move(destroy_shared_image_cb).Run(sync_token);

  // Release |image|.
  stub_->channel()->scheduler()->ScheduleTask(gpu::Scheduler::Task(
      wait_sequence_id_, base::DoNothingWithBoundArgs(std::move(image)),
      std::vector<gpu::SyncToken>({sync_token})));
}

}  // namespace media
