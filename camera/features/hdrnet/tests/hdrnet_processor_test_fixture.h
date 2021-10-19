/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#ifndef CAMERA_FEATURES_HDRNET_TESTS_HDRNET_PROCESSOR_TEST_FIXTURE_H_
#define CAMERA_FEATURES_HDRNET_TESTS_HDRNET_PROCESSOR_TEST_FIXTURE_H_

#include "features/hdrnet/hdrnet_processor_impl.h"

#include <memory>
#include <vector>

#include <base/test/task_environment.h>

#include "cros-camera/camera_buffer_manager.h"
#include "gpu/test_support/gl_test_fixture.h"

namespace cros {

// Test fixture for running tests on the HDRnet processing pipeline.
class HdrNetProcessorTestFixture {
 public:
  HdrNetProcessorTestFixture(const Size& input_size,
                             uint32_t input_hal_pixel_format,
                             const std::vector<Size>& output_sizes,
                             bool use_default_adapter);
  ~HdrNetProcessorTestFixture();

  // Loads input image with the contents in |input_file_path|. By default the
  // input image is populated with a test pattern.
  void LoadInputFile(base::FilePath input_file_path);

  // Produces a fake capture result that can be used in the test.
  Camera3CaptureDescriptor ProduceFakeCaptureResult();

  // Dumps the input and output buffers with |file_prefix| prepended to the
  // dumped file name.
  void DumpBuffers(const char* file_prefix);

  HdrNetProcessorImpl* processor() const { return processor_.get(); }
  const SharedImage& input_image() const { return input_image_; }
  std::vector<buffer_handle_t> output_buffers() const {
    std::vector<buffer_handle_t> output_buffers;
    for (const auto& b : output_buffers_) {
      output_buffers.push_back(*b.get());
    }
    return output_buffers;
  }

 protected:
  base::test::SingleThreadTaskEnvironment task_environment_;
  GlTestFixture gl_test_fixture_;
  std::unique_ptr<HdrNetProcessorImpl> processor_;
  ScopedBufferHandle input_buffer_;
  SharedImage input_image_;
  std::vector<ScopedBufferHandle> output_buffers_;

  // Fake data for testing.
  uint32_t frame_number_ = 0;
  android::CameraMetadata result_metadata_;
};

}  // namespace cros

#endif  // CAMERA_FEATURES_HDRNET_TESTS_HDRNET_PROCESSOR_TEST_FIXTURE_H_
