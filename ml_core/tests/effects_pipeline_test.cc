// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stdlib.h>

#include <base/command_line.h>
#include <base/files/file_util.h>
#include <base/synchronization/waitable_event.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "ml_core/dlc/dlc_loader.h"
#include "ml_core/effects_pipeline.h"
#include "ml_core/tests/png_io.h"
#include "ml_core/tests/test_utilities.h"

namespace {

std::atomic<bool> effect_set_success = false;
base::WaitableEvent waitable(base::WaitableEvent::ResetPolicy::MANUAL,
                             base::WaitableEvent::InitialState::NOT_SIGNALED);

base::FilePath DlcPath;

class EffectsPipelineTest : public ::testing::Test {
 protected:
  void SetUp() override {
    waitable.Reset();
    effect_set_success = false;
    pipeline_ = cros::EffectsPipeline::Create(DlcPath);
  }

  std::unique_ptr<cros::EffectsPipeline> pipeline_;
  ImageObserver observer_();
};

void SetEffectCallback(bool success) {
  if (success)
    effect_set_success = true;
  waitable.Signal();
}

bool WaitForEffectSetAndReset() {
  waitable.Wait();
  bool tmp = effect_set_success;
  waitable.Reset();
  effect_set_success = false;
  return tmp;
}

TEST_F(EffectsPipelineTest, SetEffectWithCallback) {
  cros::EffectsConfig config{.effect =
                                 cros::mojom::CameraEffect::kBackgroundBlur};
  pipeline_->SetEffect(&config, SetEffectCallback);

  EXPECT_TRUE(WaitForEffectSetAndReset());
}

TEST_F(EffectsPipelineTest, RotateThroughAllEffects) {
  cros::EffectsConfig effects_config;

  for (int i = 0; i < static_cast<int>(cros::mojom::CameraEffect::kCount);
       ++i) {
    effects_config.effect = static_cast<cros::mojom::CameraEffect>(i);
    pipeline_->SetEffect(&effects_config, SetEffectCallback);
    ASSERT_TRUE(WaitForEffectSetAndReset());
  }
}

TEST_F(EffectsPipelineTest, NoEffectLeavesFrameUnaltered) {
  const base::FilePath input_file(
      "/usr/local/share/ml_core/tom_sample_720.png");

  PngImageIO pngio_ = PngImageIO();
  auto info = pngio_.ReadPngFile(input_file).value();

  std::unique_ptr<uint8_t> frame_data_buf(
      new uint8_t[info.num_row_bytes * info.height]);
  ImageFrame frame{.frame_data = frame_data_buf.get(),
                   .frame_width = 0,
                   .frame_height = 0,
                   .stride = 0};
  pipeline_->SetRenderedImageObserver(std::make_unique<ImageObserver>(&frame));

  std::unique_ptr<uint8_t> raw_data_buf(
      new uint8_t[info.num_row_bytes * info.height]);
  ASSERT_TRUE(
      info.GetRawData(raw_data_buf.get(), info.num_row_bytes * info.height));

  pipeline_->ProcessFrame(1, raw_data_buf.get(), info.width, info.height,
                          info.num_row_bytes);
  pipeline_->Wait();

  EXPECT_EQ(frame.frame_height, info.height);
  EXPECT_EQ(frame.frame_width, info.width);
  EXPECT_EQ(frame.stride, info.num_row_bytes);
  EXPECT_TRUE(FuzzyBufferComparison(frame.frame_data, raw_data_buf.get(),
                                    info.num_row_bytes * info.height));
}

TEST_F(EffectsPipelineTest, BlurEffectBlursImage) {
  const base::FilePath input_file(
      "/usr/local/share/ml_core/tom_sample_720.png");
  const base::FilePath reference_file(
      "/usr/local/share/ml_core/tom_blur_720_hd.png");

  PngImageIO pngio_ = PngImageIO();
  auto input_info = pngio_.ReadPngFile(input_file).value();

  std::unique_ptr<uint8_t> frame_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ImageFrame frame{.frame_data = frame_data_buf.get(),
                   .frame_width = 0,
                   .frame_height = 0,
                   .stride = 0};
  pipeline_->SetRenderedImageObserver(std::make_unique<ImageObserver>(&frame));

  cros::EffectsConfig effects_config;
  effects_config.effect = cros::mojom::CameraEffect::kBackgroundBlur;
  pipeline_->SetEffect(&effects_config, SetEffectCallback);
  ASSERT_TRUE(WaitForEffectSetAndReset());

  std::unique_ptr<uint8_t> raw_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ASSERT_TRUE(input_info.GetRawData(
      raw_data_buf.get(), input_info.num_row_bytes * input_info.height));

  pipeline_->ProcessFrame(1, raw_data_buf.get(), input_info.width,
                          input_info.height, input_info.num_row_bytes);
  pipeline_->Wait();

  auto ref_info = pngio_.ReadPngFile(reference_file).value();
  ASSERT_TRUE(ref_info.GetRawData(raw_data_buf.get(),
                                  ref_info.num_row_bytes * ref_info.height));

  EXPECT_EQ(frame.frame_height, ref_info.height);
  EXPECT_EQ(frame.frame_width, ref_info.width);
  EXPECT_EQ(frame.stride, ref_info.num_row_bytes);

  const int kAcceptablePixelDelta = 5;
  const int kNumAcceptOutsideDelta = 2000;
  // This comparison allows for bytes to differ by 5 in their channel
  // values and allow up to 2000 channel bytes to be higher
  // 2000 bytes is 0.05% of images total pixels
  EXPECT_TRUE(FuzzyBufferComparison(frame.frame_data, raw_data_buf.get(),
                                    ref_info.num_row_bytes * ref_info.height,
                                    kAcceptablePixelDelta,
                                    kNumAcceptOutsideDelta));
}

TEST_F(EffectsPipelineTest, RelightEffectRelightsImage) {
  const base::FilePath input_file(
      "/usr/local/share/ml_core/tom_sample_720.png");
  const base::FilePath reference_file(
      "/usr/local/share/ml_core/tom_relight_auto_720_hd.png");

  PngImageIO pngio_ = PngImageIO();
  auto input_info = pngio_.ReadPngFile(input_file).value();

  std::unique_ptr<uint8_t> frame_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ImageFrame frame{.frame_data = frame_data_buf.get(),
                   .frame_width = 0,
                   .frame_height = 0,
                   .stride = 0};
  pipeline_->SetRenderedImageObserver(std::make_unique<ImageObserver>(&frame));

  cros::EffectsConfig effects_config;
  effects_config.effect = cros::mojom::CameraEffect::kPortraitRelight;
  pipeline_->SetEffect(&effects_config, SetEffectCallback);
  ASSERT_TRUE(WaitForEffectSetAndReset());

  std::unique_ptr<uint8_t> raw_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ASSERT_TRUE(input_info.GetRawData(
      raw_data_buf.get(), input_info.num_row_bytes * input_info.height));

  // Need to send two packets as relight uses information
  // from the packets N and N-1 to create the effect
  pipeline_->ProcessFrame(1, raw_data_buf.get(), input_info.width,
                          input_info.height, input_info.num_row_bytes);
  pipeline_->Wait();
  pipeline_->ProcessFrame(2, raw_data_buf.get(), input_info.width,
                          input_info.height, input_info.num_row_bytes);
  pipeline_->Wait();

  auto ref_info = pngio_.ReadPngFile(reference_file).value();
  ASSERT_TRUE(ref_info.GetRawData(raw_data_buf.get(),
                                  ref_info.num_row_bytes * ref_info.height));

  EXPECT_EQ(frame.frame_height, ref_info.height);
  EXPECT_EQ(frame.frame_width, ref_info.width);
  EXPECT_EQ(frame.stride, ref_info.num_row_bytes);

  const int kAcceptablePixelDelta = 5;
  const int kNumAcceptOutsideDelta = 2000;
  EXPECT_TRUE(FuzzyBufferComparison(frame.frame_data, raw_data_buf.get(),
                                    ref_info.num_row_bytes * ref_info.height,
                                    kAcceptablePixelDelta,
                                    kNumAcceptOutsideDelta));
}

TEST_F(EffectsPipelineTest, BlurEffectWithExtraBlurLevel) {
  const base::FilePath input_file(
      "/usr/local/share/ml_core/tom_sample_720.png");
  const base::FilePath reference_file(
      "/usr/local/share/ml_core/tom_maximum_blur_720_hd.png");

  PngImageIO pngio_ = PngImageIO();
  auto input_info = pngio_.ReadPngFile(input_file).value();

  std::unique_ptr<uint8_t> frame_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ImageFrame frame{.frame_data = frame_data_buf.get(),
                   .frame_width = 0,
                   .frame_height = 0,
                   .stride = 0};
  pipeline_->SetRenderedImageObserver(std::make_unique<ImageObserver>(&frame));

  cros::EffectsConfig effects_config;
  effects_config.effect = cros::mojom::CameraEffect::kBackgroundBlur;
  pipeline_->SetEffect(&effects_config, SetEffectCallback);
  EXPECT_TRUE(WaitForEffectSetAndReset());

  std::unique_ptr<uint8_t> raw_data_buf(
      new uint8_t[input_info.num_row_bytes * input_info.height]);
  ASSERT_TRUE(input_info.GetRawData(
      raw_data_buf.get(), input_info.num_row_bytes * input_info.height));

  pipeline_->ProcessFrame(1, raw_data_buf.get(), input_info.width,
                          input_info.height, input_info.num_row_bytes);
  pipeline_->Wait();

  effects_config.blur_level = cros::mojom::BlurLevel::kMaximum;

  pipeline_->SetEffect(&effects_config, SetEffectCallback);
  ASSERT_TRUE(WaitForEffectSetAndReset());

  pipeline_->ProcessFrame(2, raw_data_buf.get(), input_info.width,
                          input_info.height, input_info.num_row_bytes);
  pipeline_->Wait();

  auto ref_info = pngio_.ReadPngFile(reference_file).value();
  ASSERT_TRUE(ref_info.GetRawData(raw_data_buf.get(),
                                  ref_info.num_row_bytes * ref_info.height));

  EXPECT_EQ(frame.frame_height, ref_info.height);
  EXPECT_EQ(frame.frame_width, ref_info.width);
  EXPECT_EQ(frame.stride, ref_info.num_row_bytes);

  const int kAcceptablePixelDelta = 5;
  const int kNumAcceptOutsideDelta = 2500;
  EXPECT_TRUE(FuzzyBufferComparison(frame.frame_data, raw_data_buf.get(),
                                    ref_info.num_row_bytes * ref_info.height,
                                    kAcceptablePixelDelta,
                                    kNumAcceptOutsideDelta));
}

}  // namespace

int main(int argc, char* argv[]) {
  base::CommandLine::Init(argc, argv);
  base::CommandLine* cl = base::CommandLine::ForCurrentProcess();
  if (cl->HasSwitch("nodlc")) {
    DlcPath = base::FilePath("/usr/local/lib64");
  } else {
    cros::DlcLoader client;
    client.Run();
    if (!client.DlcLoaded()) {
      LOG(ERROR) << "Failed to load DLC";
      return -1;
    }
    DlcPath = client.GetDlcRootPath();
  }

  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
