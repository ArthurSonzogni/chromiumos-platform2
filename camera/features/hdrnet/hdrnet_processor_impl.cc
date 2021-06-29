/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "features/hdrnet/hdrnet_processor_impl.h"

#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <hardware/gralloc.h>
#include <sync/sync.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_buffer_utils.h"
#include "cros-camera/camera_metadata_utils.h"
#include "cros-camera/common.h"
#include "gpu/egl/egl_fence.h"

namespace cros {

namespace {

constexpr const char kModelDir[] = "/opt/google/cros-camera/ml_models/hdrnet";

}  // namespace

// static
std::unique_ptr<HdrNetProcessor> HdrNetProcessorImpl::CreateInstance(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner) {
  return std::make_unique<HdrNetProcessorImpl>(
      static_info, task_runner,
      HdrNetProcessorDeviceAdapter::CreateInstance(static_info, task_runner));
}

HdrNetProcessorImpl::HdrNetProcessorImpl(
    const camera_metadata_t* static_info,
    scoped_refptr<base::SingleThreadTaskRunner> task_runner,
    std::unique_ptr<HdrNetProcessorDeviceAdapter> processor_device_adapter)
    : task_runner_(task_runner),
      processor_device_adapter_(std::move(processor_device_adapter)) {}

bool HdrNetProcessorImpl::Initialize(Size input_size,
                                     const std::vector<Size>& output_sizes) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  for (const auto& s : output_sizes) {
    if (s.width > input_size.width || s.height > input_size.height) {
      LOGF(ERROR) << "Output size " << s.ToString()
                  << " has larger dimension than the input size "
                  << input_size.ToString();
      return false;
    }
  }

  image_processor_ = std::make_unique<GpuImageProcessor>();
  if (!image_processor_) {
    LOGF(ERROR) << "Failed to create GpuImageProcessor";
    return false;
  }

  if (!processor_device_adapter_->Initialize()) {
    LOGF(ERROR) << "Failed to initialized HdrNetProcessorDeviceAdapter";
    return false;
  }

  VLOGF(1) << "Create HDRnet pipeline with: input_width=" << input_size.width
           << " input_height=" << input_size.height
           << " output_width=" << input_size.width
           << " output_height=" << input_size.height;
  HdrNetLinearRgbPipelineCrOS::Options options{
      .input_width = static_cast<int>(input_size.width),
      .input_height = static_cast<int>(input_size.height),
      .output_width = static_cast<int>(input_size.width),
      .output_height = static_cast<int>(input_size.height),
  };
  std::string model_dir = "";
  if (base::PathExists(base::FilePath(kModelDir))) {
    model_dir = kModelDir;
  }
  hdrnet_pipeline_ =
      HdrNetLinearRgbPipelineCrOS::CreatePipeline(options, model_dir);
  if (!hdrnet_pipeline_) {
    LOGF(ERROR) << "Failed to create HDRnet pipeline";
    return false;
  }

  for (auto& i : intermediates_) {
    i = SharedImage::CreateFromGpuTexture(GL_RGBA32F, input_size.width,
                                          input_size.height);
  }

  return true;
}

void HdrNetProcessorImpl::TearDown() {
  DCHECK(task_runner_->BelongsToCurrentThread());

  processor_device_adapter_->TearDown();
}

void HdrNetProcessorImpl::ProcessResultMetadata(
    int frame_number, const camera_metadata_t* metadata) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  processor_device_adapter_->ProcessResultMetadata(frame_number, metadata);
}

base::ScopedFD HdrNetProcessorImpl::Run(
    int frame_number,
    const HdrNetConfig::Options& options,
    const SharedImage& input_yuv,
    base::ScopedFD input_release_fence,
    const std::vector<buffer_handle_t>& output_nv12_buffers) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  for (const auto& b : output_nv12_buffers) {
    if (CameraBufferManager::GetWidth(b) > input_yuv.y_texture().width() ||
        CameraBufferManager::GetHeight(b) > input_yuv.y_texture().height()) {
      LOGF(ERROR) << "Output buffer has larger dimension than the input buffer";
      return base::ScopedFD();
    }
  }

  std::vector<SharedImage> output_images;
  for (const auto& b : output_nv12_buffers) {
    SharedImage output_nv12 =
        SharedImage::CreateFromBuffer(b, Texture2D::Target::kTarget2D,
                                      /*separate_yuv_textures=*/true);
    if (!output_nv12.y_texture().IsValid() ||
        !output_nv12.uv_texture().IsValid()) {
      LOGF(ERROR) << "Failed to create Y/UV texture for the output buffer";
      // TODO(jcliang): We should probably find a way to return a result error
      // here.
      continue;
    }
    output_images.emplace_back(std::move(output_nv12));
  }

  if (input_release_fence.is_valid()) {
    sync_wait(input_release_fence.get(), 300);
  }

  if (!options.hdrnet_enable) {
    // Convert to NV12 directly.
    for (const auto& output_nv12 : output_images) {
      YUVToNV12(input_yuv, output_nv12);
    }
  } else {
    bool success = false;
    do {
      // Run the HDRnet pipeline.
      success = processor_device_adapter_->Preprocess(options, input_yuv,
                                                      intermediates_[0]);
      if (options.dump_buffer) {
        DumpGpuTextureSharedImage(
            intermediates_[0],
            base::FilePath(base::StringPrintf(
                "preprocess_out_rgba_%dx%d_result#%d.bin",
                intermediates_[0].texture().width(),
                intermediates_[0].texture().height(), frame_number)));
      }
      if (!success) {
        LOGF(ERROR) << "Failed to pre-process HDRnet pipeline input";
        break;
      }
      success =
          RunLinearRgbPipeline(options, intermediates_[0], intermediates_[1]);
      if (!success) {
        LOGF(ERROR) << "Failed to run HDRnet pipeline";
        break;
      }
      if (options.dump_buffer) {
        DumpGpuTextureSharedImage(
            intermediates_[1],
            base::FilePath(base::StringPrintf(
                "linear_rgb_pipeline_out_rgba_%dx%d_result#%d.bin",
                intermediates_[1].texture().width(),
                intermediates_[1].texture().width(), frame_number)));
      }
      for (const auto& output_nv12 : output_images) {
        // Here we assume all the streams have the same aspect ratio, so no
        // cropping is done.
        success = processor_device_adapter_->Postprocess(
            options, intermediates_[1], output_nv12);
        if (!success) {
          LOGF(ERROR) << "Failed to post-process HDRnet pipeline output";
          break;
        }
        if (options.dump_buffer) {
          glFinish();
          CameraBufferManager* buf_mgr = CameraBufferManager::GetInstance();
          do {
            if (buf_mgr->Register(output_nv12.buffer()) != 0) {
              LOGF(ERROR) << "Failed to register output NV12 buffer";
              break;
            }
            if (!WriteBufferIntoFile(
                    output_nv12.buffer(),
                    base::FilePath(base::StringPrintf(
                        "postprocess_out_nv12_%dx%d_result#%d.bin",
                        CameraBufferManager::GetWidth(output_nv12.buffer()),
                        CameraBufferManager::GetHeight(output_nv12.buffer()),
                        frame_number)))) {
              LOGF(ERROR) << "Failed to dump output NV12 buffer";
            }
            buf_mgr->Deregister(output_nv12.buffer());
          } while (0);
        }
      }
    } while (0);

    if (!success) {
      for (const auto& output_nv12 : output_images) {
        YUVToNV12(input_yuv, output_nv12);
      }
    }
  }
  EglFence fence;
  return fence.GetNativeFd();
}

void HdrNetProcessorImpl::YUVToNV12(const SharedImage& input_yuv,
                                    const SharedImage& output_nv12) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  bool result = image_processor_->NV12ToNV12(
      input_yuv.y_texture(), input_yuv.uv_texture(), output_nv12.y_texture(),
      output_nv12.uv_texture());
  if (!result) {
    VLOGF(1) << "Failed to produce NV12 output";
  }
}

HdrNetLinearRgbPipelineCrOS::Texture2DInfo CreateTextureInfo(
    const SharedImage& image) {
  return HdrNetLinearRgbPipelineCrOS::Texture2DInfo{
      .id = base::checked_cast<GLint>(image.texture().handle()),
      .internal_format = GL_RGBA16F,
      .width = image.texture().width(),
      .height = image.texture().height()};
}

bool HdrNetProcessorImpl::RunLinearRgbPipeline(
    const HdrNetConfig::Options& options,
    const SharedImage& input_rgba,
    const SharedImage& output_rgba) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  // Run the HDRnet linear RGB pipeline
  bool result =
      hdrnet_pipeline_->Run(CreateTextureInfo(input_rgba),
                            HdrNetLinearRgbPipelineCrOS::Texture2DInfo(),
                            CreateTextureInfo(output_rgba), options.hdr_ratio);
  if (!result) {
    LOGF(WARNING) << "Failed to run HDRnet pipeline";
    return false;
  }
  return true;
}

void HdrNetProcessorImpl::DumpGpuTextureSharedImage(
    const SharedImage& image, base::FilePath output_file_path) {
  DCHECK(task_runner_->BelongsToCurrentThread());

  uint32_t kDumpBufferUsage = GRALLOC_USAGE_SW_WRITE_OFTEN |
                              GRALLOC_USAGE_SW_READ_OFTEN |
                              GRALLOC_USAGE_HW_TEXTURE;
  int image_width = image.texture().width(),
      image_height = image.texture().height();
  if (!dump_buffer_ ||
      (CameraBufferManager::GetWidth(*dump_buffer_) != image_width) ||
      (CameraBufferManager::GetHeight(*dump_buffer_) != image_height)) {
    dump_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        image.texture().width(), image.texture().height(),
        HAL_PIXEL_FORMAT_RGBX_8888, kDumpBufferUsage);
    if (!dump_buffer_) {
      LOGF(ERROR) << "Failed to allocate dump buffer";
      return;
    }
    dump_image_ = SharedImage::CreateFromBuffer(*dump_buffer_,
                                                Texture2D::Target::kTarget2D);
    if (!dump_image_.texture().IsValid()) {
      LOGF(ERROR) << "Failed to create SharedImage for dump buffer";
      return;
    }
  }
  // Use the gamma correction shader with Gamma == 1.0 to copy the contents from
  // the GPU texture to the DMA-buf.
  image_processor_->ApplyGammaCorrection(1.0f, image.texture(),
                                         dump_image_.texture());
  glFinish();
  if (!WriteBufferIntoFile(*dump_buffer_, output_file_path)) {
    LOGF(ERROR) << "Failed to dump GPU texture";
  }
}

}  // namespace cros
