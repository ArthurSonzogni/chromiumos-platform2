// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mantis/lib_api.h"

namespace mantis {
namespace {
using base::test::TestFuture;
using mojom::MantisError;
using mojom::MantisResult;
using mojom::SafetyClassifierVerdict;
using testing::IsEmpty;

constexpr ProcessorPtr kFakeProcessorPtr = 0xDEADBEEF;
constexpr SegmenterPtr kFakeSegmenterPtr = 0xCAFEBABE;

class MantisProcessorTest : public testing::Test {
 public:
  MantisProcessorTest() { mojo::core::Init(); }

 protected:
  std::vector<uint8_t> GetFakeImage() {
    return {0x00, 0x7F, 0xFF, 0x10, 0x50, 0x90, 0x20, 0x60, 0xA0};
  }

  std::vector<uint8_t> GetFakeMask() {
    return {0x10, 0x50, 0x90, 0x20, 0x60, 0xA0, 0x00, 0x7F, 0xFF};
  }

  MantisProcessor InitializeMantisProcessor(MantisComponent component,
                                            const MantisAPI* api) {
    EXPECT_CALL(safety_service_manager_, PrepareImageSafetyClassifier)
        .WillOnce(base::test::RunOnceCallback<0>(true));
    return MantisProcessor(
        component, api, processor_remote_.BindNewPipeAndPassReceiver(),
        raw_ref(safety_service_manager_), base::DoNothing(), base::DoNothing());
  }

  base::test::TaskEnvironment task_environment_;
  mojo::Remote<mojom::MantisProcessor> processor_remote_;
  cros_safety::SafetyServiceManagerMock safety_service_manager_;
};

TEST_F(MantisProcessorTest, InpaintingMissingProcessor) {
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessorNotInitialized);
}

TEST_F(MantisProcessorTest, InpaintingInputSafetyError) {
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  std::vector<uint8_t> image;
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kInputSafetyError);
}

TEST_F(MantisProcessorTest, InpaintingProcessFailed) {
  auto inpainting = [](ProcessorPtr processor_ptr,
                       const std::vector<uint8_t>& image,
                       const std::vector<uint8_t>& mask, int seed) {
    return InpaintingResult{.status = MantisStatus::kProcessFailed};
  };
  auto destroy_mantis_component = [](MantisComponent) {};

  const MantisAPI api = {
      .Inpainting = +inpainting,
      .DestroyMantisComponent = +destroy_mantis_component,
  };

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      &api);
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  std::vector<uint8_t> image;
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
}

TEST_F(MantisProcessorTest, InpaintingOutputSafetyError) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kOutputSafetyError);
}

TEST_F(MantisProcessorTest, InpaintingGeneratedRegionFails) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kOutputSafetyError);
}

TEST_F(MantisProcessorTest, InpaintingSucceeds) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillMissingProcessor) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessorNotInitialized);
}

TEST_F(MantisProcessorTest, GenerativeFillInputSafetyError) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kInputSafetyError);
}

TEST_F(MantisProcessorTest, GenerativeFillProcessFailed) {
  auto generative_fill = [](ProcessorPtr processor_ptr,
                            const std::vector<uint8_t>& image,
                            const std::vector<uint8_t>& mask, int seed,
                            const std::string& text_prompt) {
    return GenerativeFillResult{.status = MantisStatus::kProcessFailed};
  };
  auto destroy_mantis_component = [](MantisComponent) {};

  const MantisAPI api = {
      .GenerativeFill = +generative_fill,
      .DestroyMantisComponent = +destroy_mantis_component,
  };

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      &api);

  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  std::vector<uint8_t> image;
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
}

TEST_F(MantisProcessorTest, GenerativeFillOutputSafetyError) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kOutputSafetyError);
}

TEST_F(MantisProcessorTest, GenerativeFillGeneratedRegionFails) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kOutputSafetyError);
}

TEST_F(MantisProcessorTest, GenerativeFillPromptSafetyError) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedText))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedText));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kPromptSafetyError);
}

TEST_F(MantisProcessorTest, GenerativeFillSucceeds) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, SegmentationMissingSegmenter) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Segmentation(GetFakeImage(), GetFakeMask(),
                         result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kMissingSegmenter);
}

TEST_F(MantisProcessorTest, SegmentationInputSafetyError) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = kFakeSegmenterPtr,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Segmentation(GetFakeImage(), GetFakeMask(),
                         result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kInputSafetyError);
}

TEST_F(MantisProcessorTest, SegmentationReturnError) {
  auto segmentation = [](ProcessorPtr processor_ptr,
                         const std::vector<uint8_t>& image,
                         const std::vector<uint8_t>& prior) {
    return SegmentationResult{.status = MantisStatus::kProcessFailed};
  };
  auto destroy_mantis_component = [](MantisComponent) {};

  const MantisAPI api = {
      .Segmentation = +segmentation,
      .DestroyMantisComponent = +destroy_mantis_component,
  };

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = kFakeSegmenterPtr,
      },
      &api);
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  std::vector<uint8_t> image;
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Segmentation(GetFakeImage(), GetFakeMask(),
                         result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
}

TEST_F(MantisProcessorTest, SegmentationSucceeds) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = kFakeSegmenterPtr,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Segmentation(GetFakeImage(), GetFakeMask(),
                         result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, ClassifyImageSafetyReturnPass) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  TestFuture<mojom::SafetyClassifierVerdict> verdict_future;
  processor.ClassifyImageSafety(GetFakeImage(), verdict_future.GetCallback());

  auto verdict = verdict_future.Take();
  EXPECT_EQ(verdict, SafetyClassifierVerdict::kPass);
}

TEST_F(MantisProcessorTest, ClassifyImageSafetyReturnFail) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = 0,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kFailedImage));

  TestFuture<mojom::SafetyClassifierVerdict> verdict_future;
  processor.ClassifyImageSafety(GetFakeImage(), verdict_future.GetCallback());

  auto verdict = verdict_future.Take();
  EXPECT_EQ(verdict, SafetyClassifierVerdict::kFailedImage);
}

}  // namespace
}  // namespace mantis
