// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/processor.h"

#include <base/memory/raw_ref.h>
#include <base/task/sequenced_task_runner.h>
#include <base/test/bind.h>
#include <base/test/gmock_callback_support.h>
#include <base/test/task_environment.h>
#include <base/test/test_future.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/core/embedder/embedder.h>
#include <testing/gmock/include/gmock/gmock.h>
#include <testing/gtest/include/gtest/gtest.h>

#include "metrics/metrics_library_mock.h"
#include "odml/cros_safety/safety_service_manager_mock.h"
#include "odml/i18n/mock_language_detector.h"
#include "odml/i18n/mock_translator.h"
#include "odml/mantis/common.h"
#include "odml/mantis/fake/fake_mantis_api.h"
#include "odml/mantis/lib_api.h"
#include "odml/mojom/cros_safety.mojom.h"
#include "odml/periodic_metrics.h"

namespace mantis {
namespace {
using base::test::TestFuture;
using mojom::MantisError;
using mojom::MantisResult;
using mojom::SafetyClassifierVerdict;
using mojom::TouchPoint;
using mojom::TouchPointPtr;
using ::testing::_;
using ::testing::IsEmpty;
using ::testing::WithArg;
using LanguageDetectonResult =
    std::vector<on_device_model::LanguageDetector::TextLanguage>;

constexpr ProcessorPtr kFakeProcessorPtr = 0xDEADBEEF;
constexpr SegmenterPtr kFakeSegmenterPtr = 0xCAFEBABE;

class MantisProcessorTest : public testing::Test {
 public:
  MantisProcessorTest() {
    mojo::core::Init();

    // Bypass translation flow by detecting English.
    ON_CALL(language_detector_, Classify)
        .WillByDefault(base::test::RunOnceCallback<1>(LanguageDetectonResult(
            {{.locale = kEnglishLocale, .confidence = 1.0}})));
    // Bypass T&S
    ON_CALL(safety_service_manager_, ClassifyImageSafety)
        .WillByDefault(base::test::RunOnceCallbackRepeatedly<3>(
            cros_safety::mojom::SafetyClassifierVerdict::kPass));
  }

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
        raw_ref(metrics_lib_), raw_ref(periodic_metrics_),
        base::SequencedTaskRunner::GetCurrentDefault(), component, api,
        processor_remote_.BindNewPipeAndPassReceiver(),
        raw_ref(safety_service_manager_), raw_ref(language_detector_),
        raw_ref(translator_), base::DoNothing(), base::DoNothing());
  }

  // Checks the final operation type. For example, some Generative Fill requests
  // can be routed to Inpainting.
  void ExpectFinalOperationType(OperationType operation_type) {
    CHECK(operation_type == OperationType::kGenfill ||
          operation_type == OperationType::kInpainting);

    // Currently, it is infeasible to mock MantisAPI to check the final
    // operation type. This is a workaround by checking the metric being sent.
    std::string uma_string_name =
        "Platform.MantisService.Latency.GenerativeFill";
    if (operation_type == OperationType::kInpainting) {
      uma_string_name = "Platform.MantisService.Latency.Inpainting";
    }
    EXPECT_CALL(metrics_lib_, SendTimeToUMA(uma_string_name, _, _, _, _));
    // This T&S metric will always be called regardless of the operation type.
    EXPECT_CALL(
        metrics_lib_,
        SendTimeToUMA("Platform.MantisService.Latency.ClassifyImageSafety", _,
                      _, _, _))
        .Times(2);
  }

  void ExpectFinalPrompt(std::string final_prompt) {
    // Currently, it is infeasible to mock MantisAPI to check the final prompt.
    // This is a workaround by checking the prompt sent for T&S.
    EXPECT_CALL(
        safety_service_manager_,
        ClassifyImageSafety(
            cros_safety::mojom::SafetyRuleset::kMantisGeneratedRegion, _, _, _))
        .WillOnce(base::test::RunOnceCallback<3>(
            cros_safety::mojom::SafetyClassifierVerdict::kPass));
    EXPECT_CALL(safety_service_manager_,
                ClassifyImageSafety(
                    cros_safety::mojom::SafetyRuleset::kMantisOutputImage,
                    std::optional(final_prompt), _, _))
        .WillOnce(base::test::RunOnceCallback<3>(
            cros_safety::mojom::SafetyClassifierVerdict::kPass));
  }

  base::test::TaskEnvironment task_environment_{
      base::test::TaskEnvironment::TimeSource::MOCK_TIME};
  testing::NiceMock<MetricsLibraryMock> metrics_lib_;
  odml::PeriodicMetrics periodic_metrics_{raw_ref(metrics_lib_)};
  mojo::Remote<mojom::MantisProcessor> processor_remote_;
  cros_safety::SafetyServiceManagerMock safety_service_manager_;
  on_device_model::MockLanguageDetector language_detector_;
  i18n::MockTranslator translator_;
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

TEST_F(MantisProcessorTest, InpaintingProccesingFailedNoInternet) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kNoInternetConnection))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kNoInternetConnection));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
}

TEST_F(MantisProcessorTest, InpaintingProccesingFailedServiceNotAvailable) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kServiceNotAvailable))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kServiceNotAvailable));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
}

TEST_F(MantisProcessorTest, InpaintingProccesingFailedBackendFailure) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kBackendFailure))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kBackendFailure));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_error());
  EXPECT_EQ(result->get_error(), MantisError::kProcessFailed);
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
      .Times(2)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.ClassifyImageSafety", _, _,
                    _, _))
      .Times(2);
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.Inpainting", _, _, _, _));

  EXPECT_CALL(
      metrics_lib_,
      SendEnumToUMA("Platform.MantisService.ImageGenerationType", _, _));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, LatencyMetricOnlyIncludesInferenceTime) {
  MantisProcessor processor = InitializeMantisProcessor(
      {.processor = kFakeProcessorPtr, .segmenter = 0}, fake::GetMantisApi());
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.ClassifyImageSafety", _, _,
                    _, _))
      .Times(4);
  // Simulate long latency when sending a metric, with instant inference time.
  // This is easier to set than having a custom fake MantisApi due to C function
  // pointer limitation on capturing variables.
  const base::TimeDelta kSendMetricLatency = base::Seconds(10);
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.Inpainting",
                    // Expect 0 duration sent for instant inference time.
                    base::Seconds(0), _, _, _))
      .Times(2)
      .WillRepeatedly(WithArg<1>([&](base::TimeDelta sample) {
        // Simulate long latency when sending a metric.
        task_environment_.FastForwardBy(kSendMetricLatency);
        return true;
      }));

  // Call Inpainting twice, back-to-back. This ensures queue time is not
  // included in the latency metric as well.
  TestFuture<mojom::MantisResultPtr> result_future1, result_future2;
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future1.GetCallback());
  processor.Inpainting(GetFakeImage(), GetFakeMask(), 0,
                       result_future2.GetCallback());

  // Wait for both asynchronous calls to complete and check results.
  auto result1 = result_future1.Take();
  auto result2 = result_future2.Take();
  ASSERT_TRUE(result1->is_result_image());
  ASSERT_TRUE(result2->is_result_image());
}

TEST_F(MantisProcessorTest, OutpaintingSucceeds) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(safety_service_manager_, ClassifyImageSafety)
      .Times(2)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.ClassifyImageSafety", _, _,
                    _, _))
      .Times(2);
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.Outpainting", _, _, _, _));

  EXPECT_CALL(
      metrics_lib_,
      SendEnumToUMA("Platform.MantisService.ImageGenerationType", _, _));
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.Outpainting(GetFakeImage(), GetFakeMask(), 0,
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
      .Times(2)
      .WillRepeatedly(base::test::RunOnceCallbackRepeatedly<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.ClassifyImageSafety", _, _,
                    _, _))
      .Times(2);
  EXPECT_CALL(metrics_lib_,
              SendTimeToUMA("Platform.MantisService.Latency.GenerativeFill", _,
                            _, _, _));
  EXPECT_CALL(
      metrics_lib_,
      SendEnumToUMA("Platform.MantisService.ImageGenerationType", _, _));

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "a cute cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillI18nUnknownlanguage) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(language_detector_, Classify)
      .WillOnce(base::test::RunOnceCallback<1>(LanguageDetectonResult({})));
  // Should pass the prompt as is.
  ExpectFinalPrompt("$1abc@ &2#");

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "$1abc@ &2#",
                           result_future.GetCallback());

  auto result = result_future.Take();
  // Should get the non-error result from the original prompt.
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillI18nUnsupportedlanguage) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(language_detector_, Classify)
      .WillOnce(base::test::RunOnceCallback<1>(
          LanguageDetectonResult({{.locale = "pt", .confidence = 1.0}})));
  // Should pass the prompt as is.
  ExpectFinalPrompt("pequeno lago");

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "pequeno lago",
                           result_future.GetCallback());

  auto result = result_future.Take();
  // Should get the non-error result from the original prompt.
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillI18nSucceeds) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());
  EXPECT_CALL(language_detector_, Classify)
      .WillOnce(base::test::RunOnceCallback<1>(
          LanguageDetectonResult({{.locale = "fr", .confidence = 1.0}})));
  EXPECT_CALL(translator_, Translate)
      .WillOnce(base::test::RunOnceCallback<2>("small pond"));
  ExpectFinalPrompt("small pond");

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "petit étang",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillRewriteUserPrompt) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  ExpectFinalOperationType(OperationType::kGenfill);
  ExpectFinalPrompt("the cute cat");

  // Test one of the cases to confirm rewrite is active.
  // All other cases are tested in the unit test of the utility function.
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "Add the Cute Cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillRemainsOnEmptyPrompt) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  // Remains Genfill instead of Inpainting on empty prompt.
  ExpectFinalOperationType(OperationType::kGenfill);
  ExpectFinalPrompt("");

  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, GenerativeFillBecomesInpaintingAfterRewrite) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  // With stopword, the rewritten prompt is empty and we should do Inpainting.
  ExpectFinalOperationType(OperationType::kInpainting);

  constexpr char kStopword[] = "eliminate";
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, kStopword,
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
  EXPECT_CALL(
      metrics_lib_,
      SendTimeToUMA("Platform.MantisService.Latency.Segmentation", _, _, _, _));

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

  EXPECT_CALL(
      safety_service_manager_,
      ClassifyImageSafety(
          testing::Eq(cros_safety::mojom::SafetyRuleset::kMantisInputImage),
          testing::Eq(""), testing::_, testing::_))
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

TEST_F(MantisProcessorTest, RewriteUserPrompt) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;
  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  EXPECT_CALL(
      safety_service_manager_,
      ClassifyImageSafety(
          testing::Eq(cros_safety::mojom::SafetyRuleset::kMantisOutputImage),
          testing::Eq("the cute cat"), testing::_, testing::_))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  EXPECT_CALL(
      safety_service_manager_,
      ClassifyImageSafety(
          testing::Eq(
              cros_safety::mojom::SafetyRuleset::kMantisGeneratedRegion),
          testing::Eq(""), testing::_, testing::_))
      .WillOnce(base::test::RunOnceCallback<3>(
          cros_safety::mojom::SafetyClassifierVerdict::kPass));

  // Test one of the cases to confirm rewrite is active.
  // All other cases are tested in the unit test of the utility function.
  TestFuture<mojom::MantisResultPtr> result_future;
  processor.GenerativeFill(GetFakeImage(), GetFakeMask(), 0, "Add the Cute Cat",
                           result_future.GetCallback());

  auto result = result_future.Take();
  ASSERT_TRUE(result->is_result_image());
  EXPECT_THAT(result->get_result_image(), Not(IsEmpty()));
}

TEST_F(MantisProcessorTest, InferSegmentationModeSinglePoint) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;

  std::vector<mojom::TouchPointPtr> gesture;
  gesture.emplace_back(TouchPoint::New(0.1, 0.2));

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::SegmentationMode> mode_future;
  processor.InferSegmentationMode(std::move(gesture),
                                  mode_future.GetCallback());

  EXPECT_EQ(mode_future.Take(), mojom::SegmentationMode::kScribble);
}

TEST_F(MantisProcessorTest, InferSegmentationModeStraightLine) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;

  std::vector<mojom::TouchPointPtr> gesture;
  gesture.emplace_back(TouchPoint::New(0.1, 0.2));
  gesture.emplace_back(TouchPoint::New(0.3, 0.3));
  gesture.emplace_back(TouchPoint::New(0.5, 0.4));
  gesture.emplace_back(TouchPoint::New(0.7, 0.5));
  gesture.emplace_back(TouchPoint::New(0.9, 0.6));
  gesture.emplace_back(TouchPoint::New(1.1, 0.7));
  gesture.emplace_back(TouchPoint::New(1.3, 0.8));
  gesture.emplace_back(TouchPoint::New(1.5, 0.9));

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::SegmentationMode> mode_future;
  processor.InferSegmentationMode(std::move(gesture),
                                  mode_future.GetCallback());

  EXPECT_EQ(mode_future.Take(), mojom::SegmentationMode::kScribble);
}

TEST_F(MantisProcessorTest, InferSegmentationModeCircle) {
  mojo::Remote<mojom::MantisProcessor> processor_remote;

  // Use the regular hexdecagon gesture to approximate a circle
  std::vector<mojom::TouchPointPtr> gesture;
  gesture.emplace_back(TouchPoint::New(1.0, 0.0));
  gesture.emplace_back(TouchPoint::New(0.9238795325112867, 0.3826834323650898));
  gesture.emplace_back(TouchPoint::New(0.7071067811865476, 0.7071067811865475));
  gesture.emplace_back(
      TouchPoint::New(0.38268343236508984, 0.9238795325112867));
  gesture.emplace_back(TouchPoint::New(6.123233995736766e-17, 1.0));
  gesture.emplace_back(
      TouchPoint::New(-0.3826834323650897, 0.9238795325112867));
  gesture.emplace_back(
      TouchPoint::New(-0.7071067811865475, 0.7071067811865476));
  gesture.emplace_back(
      TouchPoint::New(-0.9238795325112867, 0.3826834323650899));
  gesture.emplace_back(TouchPoint::New(-1.0, 1.2246467991473532e-16));
  gesture.emplace_back(
      TouchPoint::New(-0.9238795325112868, -0.38268343236508967));
  gesture.emplace_back(
      TouchPoint::New(-0.7071067811865477, -0.7071067811865475));
  gesture.emplace_back(
      TouchPoint::New(-0.38268343236509034, -0.9238795325112865));
  gesture.emplace_back(TouchPoint::New(-1.8369701987210297e-16, -1.0));
  gesture.emplace_back(TouchPoint::New(0.38268343236509, -0.9238795325112866));
  gesture.emplace_back(
      TouchPoint::New(0.7071067811865474, -0.7071067811865477));
  gesture.emplace_back(
      TouchPoint::New(0.9238795325112865, -0.3826834323650904));

  MantisProcessor processor = InitializeMantisProcessor(
      {
          .processor = kFakeProcessorPtr,
          .segmenter = 0,
      },
      fake::GetMantisApi());

  TestFuture<mojom::SegmentationMode> mode_future;
  processor.InferSegmentationMode(std::move(gesture),
                                  mode_future.GetCallback());

  EXPECT_EQ(mode_future.Take(), mojom::SegmentationMode::kLasso);
}

}  // namespace
}  // namespace mantis
