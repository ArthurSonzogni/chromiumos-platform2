/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_IMPL_H_
#define CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_IMPL_H_

#include "features/hdrnet/hdrnet_processor.h"

#include <map>
#include <memory>
#include <vector>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/hdrnet_linear_rgb_pipeline_cros.h"
#include "features/hdrnet/hdrnet_processor_device_adapter.h"

namespace cros {

// HdrNetProcessorImpl holds all the state and data structure needed to do HDR
// processing on a camera stream.
class HdrNetProcessorImpl : public HdrNetProcessor {
 public:
  // The default factory method to get the activated HdrNetProcessor instance.
  static std::unique_ptr<HdrNetProcessor> CreateInstance(
      const camera_metadata_t* static_info,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner);

  // All the methods of HdrNetProcessorImpl must be sequenced on |task_runner|.
  HdrNetProcessorImpl(
      const camera_metadata_t* static_info,
      scoped_refptr<base::SingleThreadTaskRunner> task_runner,
      std::unique_ptr<HdrNetProcessorDeviceAdapter> processor_device_adapter);

  HdrNetProcessorImpl(const HdrNetProcessorImpl& other) = delete;
  HdrNetProcessorImpl& operator=(const HdrNetProcessorImpl& other) = delete;

  // HdrNetProcessor implementations.
  ~HdrNetProcessorImpl() override = default;
  bool Initialize(Size input_size,
                  const std::vector<Size>& output_sizes) override;
  void TearDown() override;
  void ProcessResultMetadata(int frame_number,
                             const camera_metadata_t* metadata) override;
  base::ScopedFD Run(
      int frame_number,
      const HdrNetConfig::Options& options,
      const SharedImage& input_yuv,
      base::ScopedFD input_release_fence,
      const std::vector<buffer_handle_t>& output_nv12_buffers) override;

 private:
  void YUVToNV12(const SharedImage& input_yuv, const SharedImage& output_nv12);

  bool RunLinearRgbPipeline(const HdrNetConfig::Options& options,
                            const SharedImage& input_rgba,
                            const SharedImage& output_rgba);

  int GetTextureRefHandle(const SharedImage& image);

  void DumpGpuTextureSharedImage(const SharedImage& image,
                                 base::FilePath output_file_path);

  scoped_refptr<base::SingleThreadTaskRunner> task_runner_;

  std::unique_ptr<GpuImageProcessor> image_processor_;
  std::unique_ptr<HdrNetLinearRgbPipelineCrOS> hdrnet_pipeline_;
  std::unique_ptr<HdrNetProcessorDeviceAdapter> processor_device_adapter_;

  // Intermediate buffers for storing pre-processing results.
  SharedImage intermediates_[2];
  std::map<GLuint, int> texture_ref_handle_map_;
  // A buffer that's only allocated when we need to dump the intermediate images
  // for debugging.
  ScopedBufferHandle dump_buffer_ = nullptr;
  SharedImage dump_image_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_HDRNET_PROCESSOR_IMPL_H_
