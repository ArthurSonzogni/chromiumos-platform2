/*
 * Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <string>
#include <vector>

#include <base/at_exit.h>
#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/strings/string_number_conversions.h>
#include <base/strings/string_split.h>
#include <base/strings/stringprintf.h>
#include <hardware/gralloc.h>
#include <system/graphics.h>
#include <drm_fourcc.h>

#include "cros-camera/camera_buffer_manager.h"
#include "cros-camera/camera_buffer_utils.h"
#include "cros-camera/common.h"
#include "cros-camera/common_types.h"
#include "gpu/gles/texture_2d.h"
#include "gpu/image_processor.h"
#include "gpu/shared_image.h"
#include "gpu/test_support/gl_test_fixture.h"

using ::testing::TestInfo;

namespace cros {

namespace {

struct Options {
  static constexpr const char kInputSizeSwitch[] = "input-size";
  static constexpr const char kOutputSizeSwitch[] = "output-size";
  static constexpr const char kDumpBufferSwitch[] = "dump-buffer";
  static constexpr const char kInputNv12File[] = "input-nv12-file";

  Size input_size{1920, 1080};
  Size output_size{1920, 1080};
  bool dump_buffer = false;
  base::Optional<base::FilePath> input_nv12_file;
};

Options g_args;

constexpr uint32_t kNV12Format = HAL_PIXEL_FORMAT_YCbCr_420_888;
constexpr uint32_t kRGBAFormat = HAL_PIXEL_FORMAT_RGBX_8888;
constexpr uint32_t kBufferUsage = GRALLOC_USAGE_SW_READ_OFTEN |
                                  GRALLOC_USAGE_SW_WRITE_OFTEN |
                                  GRALLOC_USAGE_HW_TEXTURE;

void ParseCommandLine(int argc, char** argv) {
  base::CommandLine command_line(argc, argv);
  {
    std::string arg =
        command_line.GetSwitchValueASCII(Options::kInputSizeSwitch);
    if (!arg.empty()) {
      std::vector<std::string> arg_split = base::SplitString(
          arg, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      CHECK_EQ(arg_split.size(), 2);
      CHECK(base::StringToUint(arg_split[0], &g_args.input_size.width));
      CHECK(base::StringToUint(arg_split[1], &g_args.input_size.height));
    }
  }
  {
    std::string arg =
        command_line.GetSwitchValueASCII(Options::kOutputSizeSwitch);
    if (!arg.empty()) {
      std::vector<std::string> arg_split = base::SplitString(
          arg, "x", base::TRIM_WHITESPACE, base::SPLIT_WANT_NONEMPTY);
      CHECK_EQ(arg_split.size(), 2);
      CHECK(base::StringToUint(arg_split[0], &g_args.output_size.width));
      CHECK(base::StringToUint(arg_split[1], &g_args.output_size.height));
    }
  }
  if (command_line.HasSwitch(Options::kDumpBufferSwitch)) {
    g_args.dump_buffer = true;
  }
  {
    std::string arg = command_line.GetSwitchValueASCII(Options::kInputNv12File);
    if (!arg.empty()) {
      base::FilePath path(arg);
      CHECK(base::PathExists(path)) << ": Input NV12 file does not exist";
      g_args.input_nv12_file = path;
    }
  }
}

}  // namespace

class GlImageProcessorTest : public GlTestFixture {
 protected:
  GlImageProcessorTest() = default;
  ~GlImageProcessorTest() = default;

  void AllocateExternalNV12Input() {
    // NV12 buffer with GL_TEXTURE_EXTERNAL_OES texture.
    input_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.input_size.width, g_args.input_size.height, kNV12Format,
        kBufferUsage);
    input_image_ = SharedImage::CreateFromBuffer(
        *input_buffer_, Texture2D::Target::kTargetExternal);
    ASSERT_TRUE(input_buffer_);
    ASSERT_TRUE(input_image_.texture().IsValid());
  }

  void AllocateNV12Input() {
    // NV12 buffer with dual GL_TEXTURE_2D textures for Y and UV planes.
    input_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.input_size.width, g_args.input_size.height, kNV12Format,
        kBufferUsage);
    input_image_ = SharedImage::CreateFromBuffer(
        *input_buffer_, Texture2D::Target::kTarget2D,
        /*separate_yuv_textures=*/true);
    ASSERT_TRUE(input_buffer_);
    ASSERT_TRUE(input_image_.y_texture().IsValid());
    ASSERT_TRUE(input_image_.uv_texture().IsValid());
  }

  void AllocateNV12Output() {
    // NV12 buffer with dual GL_TEXTURE_2D textures for Y and UV planes.
    output_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.output_size.width, g_args.output_size.height, kNV12Format,
        kBufferUsage);
    output_image_ = SharedImage::CreateFromBuffer(
        *output_buffer_, Texture2D::Target::kTarget2D,
        /*separate_yuv_textures=*/true);
    ASSERT_TRUE(output_buffer_);
    ASSERT_TRUE(output_image_.y_texture().IsValid());
    ASSERT_TRUE(output_image_.uv_texture().IsValid());
  }

  void AllocateRGBAInput() {
    // RGBA buffer with GL_TEXTURE_2D texture.
    input_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.output_size.width, g_args.output_size.height, kRGBAFormat,
        kBufferUsage);
    input_image_ = SharedImage::CreateFromBuffer(*input_buffer_,
                                                 Texture2D::Target::kTarget2D);
    ASSERT_TRUE(input_buffer_);
    ASSERT_TRUE(input_image_.texture().IsValid());
  }

  void AllocateRGBAOutput() {
    // RGBA buffer with GL_TEXTURE_2D texture.
    output_buffer_ = CameraBufferManager::AllocateScopedBuffer(
        g_args.output_size.width, g_args.output_size.height, kRGBAFormat,
        kBufferUsage);
    output_image_ = SharedImage::CreateFromBuffer(*output_buffer_,
                                                  Texture2D::Target::kTarget2D);
    ASSERT_TRUE(output_buffer_);
    ASSERT_TRUE(output_image_.texture().IsValid());
  }

  void DumpInputBuffer() {
    if (!g_args.dump_buffer) {
      return;
    }
    const testing::TestInfo* const test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    std::string filename = base::StringPrintf("%sInput.bin", test_info->name());
    ASSERT_TRUE(WriteBufferIntoFile(*input_buffer_, base::FilePath(filename)));
  }

  void DumpOutputBuffer(std::string suffix = "") {
    if (!g_args.dump_buffer) {
      return;
    }
    const testing::TestInfo* const test_info =
        testing::UnitTest::GetInstance()->current_test_info();
    std::string filename =
        base::StringPrintf("%sOutput%s.bin", test_info->name(), suffix.c_str());
    ASSERT_TRUE(WriteBufferIntoFile(*output_buffer_, base::FilePath(filename)));
  }

  void LoadInputFile(base::FilePath image_file) {
    ASSERT_TRUE(ReadFileIntoBuffer(*input_buffer_, image_file));
  }

  GpuImageProcessor image_processor_;
  ScopedBufferHandle input_buffer_;
  ScopedBufferHandle output_buffer_;
  SharedImage input_image_;
  SharedImage output_image_;
};

TEST_F(GlImageProcessorTest, RGBAToNV12Test) {
  AllocateRGBAInput();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateNV12Output();
  EXPECT_TRUE(image_processor_.RGBAToNV12(input_image_.texture(),
                                          output_image_.y_texture(),
                                          output_image_.uv_texture()));
  glFinish();
  DumpOutputBuffer();
}

TEST_F(GlImageProcessorTest, ExternalYUVToNV12Test) {
  AllocateExternalNV12Input();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateNV12Output();
  EXPECT_TRUE(image_processor_.ExternalYUVToNV12(input_image_.texture(),
                                                 output_image_.y_texture(),
                                                 output_image_.uv_texture()));
  glFinish();
  DumpOutputBuffer();
}

TEST_F(GlImageProcessorTest, ExternalYUVToRGBATest) {
  AllocateExternalNV12Input();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateRGBAOutput();
  EXPECT_TRUE(image_processor_.ExternalYUVToRGBA(input_image_.texture(),
                                                 output_image_.texture()));
  glFinish();
  DumpOutputBuffer();
}

TEST_F(GlImageProcessorTest, NV12ToRGBATest) {
  AllocateNV12Input();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateRGBAOutput();
  EXPECT_TRUE(image_processor_.NV12ToRGBA(input_image_.y_texture(),
                                          input_image_.uv_texture(),
                                          output_image_.texture()));
  glFinish();
  DumpOutputBuffer();
}

TEST_F(GlImageProcessorTest, NV12ToNV12Test) {
  AllocateNV12Input();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateNV12Output();
  EXPECT_TRUE(image_processor_.YUVToYUV(
      input_image_.y_texture(), input_image_.uv_texture(),
      output_image_.y_texture(), output_image_.uv_texture()));
  glFinish();
  DumpOutputBuffer();
}

TEST_F(GlImageProcessorTest, ApplyGammaTest) {
  AllocateRGBAInput();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  AllocateRGBAOutput();
  // This should increase the pixel intensity.
  EXPECT_TRUE(image_processor_.ApplyGammaCorrection(2.2, input_image_.texture(),
                                                    output_image_.texture()));
  glFinish();
  DumpOutputBuffer("2.2");

  // This should decrease the pixel intensity.
  EXPECT_TRUE(image_processor_.ApplyGammaCorrection(
      1 / 2.2, input_image_.texture(), output_image_.texture()));
  glFinish();
  DumpOutputBuffer("1over2.2");
}

TEST_F(GlImageProcessorTest, ApplyRgbLutTest) {
  AllocateRGBAInput();
  FillTestPattern(*input_buffer_);
  DumpInputBuffer();

  // Create the RGB LUTs.
  constexpr int kLutResolution = 1024;
  std::vector<float> lut(kLutResolution);
  for (int i = 0; i < lut.size(); ++i) {
    lut[i] = static_cast<float>(kLutResolution - i) / lut.size();
  }
  Texture2D r_lut_texture(GL_R16F, 1024, 1);
  ASSERT_TRUE(r_lut_texture.IsValid());
  r_lut_texture.Bind();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1, GL_RED, GL_FLOAT,
                  lut.data());
  Texture2D g_lut_texture(GL_R16F, 1024, 1);
  ASSERT_TRUE(g_lut_texture.IsValid());
  g_lut_texture.Bind();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1, GL_RED, GL_FLOAT,
                  lut.data());
  Texture2D b_lut_texture(GL_R16F, 1024, 1);
  ASSERT_TRUE(b_lut_texture.IsValid());
  b_lut_texture.Bind();
  glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, 1024, 1, GL_RED, GL_FLOAT,
                  lut.data());

  AllocateRGBAOutput();
  EXPECT_TRUE(image_processor_.ApplyRgbLut(
      r_lut_texture, g_lut_texture, b_lut_texture, input_image_.texture(),
      output_image_.texture()));
  glFinish();
  DumpOutputBuffer();
}

}  // namespace cros

int main(int argc, char** argv) {
  base::AtExitManager exit_manager;
  ::cros::ParseCommandLine(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
