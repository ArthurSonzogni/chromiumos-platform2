// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file is used for testing multiprocess related interface of
// `MachineLearningService`. One should consider migrating the test from
// "machine_learning_service_impl_test.cc" here after the interface is made
// of multiprocess.

#include <string>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <base/test/bind.h>
#include <base/time/time.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/handwriting.h"
#include "ml/machine_learning_service_impl.h"
#include "ml/mojom/machine_learning_service.mojom.h"
#include "ml/mojom/web_platform_handwriting.mojom.h"
#include "ml/process.h"
#include "ml/test_utils.h"
#include "ml/web_platform_handwriting_proto_mojom_conversion.h"

namespace ml {
namespace {

// We intend not to using `chromeos::machine_learning::web_platform::mojom::*`
// to avoid confusion.
using ::chromeos::machine_learning::mojom::LoadHandwritingModelResult;
using ::chromeos::machine_learning::mojom::MachineLearningService;

// Points that are used to generate a stroke for handwriting.
constexpr float kHandwritingTestPoints[23][2] = {
    {1.928, 0.827}, {1.828, 0.826}, {1.73, 0.858},  {1.667, 0.901},
    {1.617, 0.955}, {1.567, 1.043}, {1.548, 1.148}, {1.569, 1.26},
    {1.597, 1.338}, {1.641, 1.408}, {1.688, 1.463}, {1.783, 1.473},
    {1.853, 1.418}, {1.897, 1.362}, {1.938, 1.278}, {1.968, 1.204},
    {1.999, 1.112}, {2.003, 1.004}, {1.984, 0.905}, {1.988, 1.043},
    {1.98, 1.178},  {1.976, 1.303}, {1.984, 1.415},
};

// A version of MachineLearningServiceImpl that loads from the testing model
// directory.
class MachineLearningServiceImplForTesting : public MachineLearningServiceImpl {
 public:
  // Pass an empty callback and use the testing model directory.
  explicit MachineLearningServiceImplForTesting(
      mojo::PendingReceiver<
          chromeos::machine_learning::mojom::MachineLearningService> receiver)
      : MachineLearningServiceImpl(
            std::move(receiver), base::OnceClosure(), GetTestModelDir()) {}
};

}  // namespace

TEST(WebPlatformHandwritingModel, LoadModelAndRecognize) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  // Loads a model.
  base::RunLoop runloop;

  // Sets the process to be control to test multiprocess code.
  Process::GetInstance()->SetTypeForTesting(Process::Type::kControlForTest);

  // Set the callback when the worker process has been reaped successfully. We
  // need to quit the runloop here.
  Process::GetInstance()->SetReapWorkerProcessSucceedCallbackForTesting(
      base::BindLambdaForTesting([&]() { runloop.Quit(); }));

  // Set the callback when the worker process fails to be reaped. We need to
  // quit the runloop here. Also we should set a flag and report the error.
  bool reap_worker_process_succeeded = true;
  std::string reap_worker_process_fail_reason;
  Process::GetInstance()->SetReapWorkerProcessFailCallbackForTesting(
      base::BindLambdaForTesting([&](std::string reason) {
        reap_worker_process_succeeded = false;
        reap_worker_process_fail_reason = reason;
        runloop.Quit();
      }));

  // Binds the disconnection handler. We need to quit the runloop here.
  Process::GetInstance()->SetReapWorkerProcessSucceedCallbackForTesting(
      base::BindLambdaForTesting([&]() { runloop.Quit(); }));

  // Sets the mlservice binary path which should be at the same dir of the test
  // binary.
  Process::GetInstance()->SetMlServicePathForTesting(GetMlServicePath());

  mojo::Remote<MachineLearningService> ml_service;
  auto ml_service_impl = std::make_unique<MachineLearningServiceImplForTesting>(
      ml_service.BindNewPipeAndPassReceiver());

  // Tries to load a model.
  mojo::Remote<
      chromeos::machine_learning::web_platform::mojom::HandwritingRecognizer>
      recognizer;

  bool model_callback_done = false;
  auto constraint = chromeos::machine_learning::web_platform::mojom::
      HandwritingModelConstraint::New();
  constraint->languages.push_back("en");
  ml_service->LoadWebPlatformHandwritingModel(
      std::move(constraint), recognizer.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](const LoadHandwritingModelResult result) {
        ASSERT_EQ(result, LoadHandwritingModelResult::OK);
        // Check the worker process is registered.
        EXPECT_EQ(Process::GetInstance()->GetWorkerPidInfoMap().size(), 1u);
        // Check the worker process is alive.
        pid_t worker_pid =
            Process::GetInstance()->GetWorkerPidInfoMap().begin()->first;
        ASSERT_GT(worker_pid, 0);
        EXPECT_EQ(kill(worker_pid, 0), 0);

        model_callback_done = true;
      }));

  // Tries to get the prediction result.
  // Set default inputs.
  auto hints =
      chromeos::machine_learning::web_platform::mojom::HandwritingHints::New();
  hints->alternatives = 1u;
  auto stroke =
      chromeos::machine_learning::web_platform::mojom::HandwritingStroke::New();
  for (int i = 0; i < 23; ++i) {
    auto point = chromeos::machine_learning::web_platform::mojom::
        HandwritingPoint::New();
    auto location = gfx::mojom::PointF::New();
    location->x = kHandwritingTestPoints[i][0];
    location->y = kHandwritingTestPoints[i][1];
    point->location = std::move(location);
    stroke->points.push_back(std::move(point));
  }
  std::vector<
      chromeos::machine_learning::web_platform::mojom::HandwritingStrokePtr>
      strokes;
  strokes.push_back(std::move(stroke));

  bool prediction_callback_done = false;
  pid_t worker_pid = -1;
  recognizer->GetPrediction(
      std::move(strokes), std::move(hints),
      base::BindLambdaForTesting(
          [&](base::Optional<
              std::vector<chromeos::machine_learning::web_platform::mojom::
                              HandwritingPredictionPtr>> predictions) {
            // Check that the inference succeeded and gives
            // the expected number of outputs.
            ASSERT_TRUE(predictions.has_value());
            ASSERT_EQ(predictions->size(), 1u);
            EXPECT_EQ(predictions->at(0)->text, "a");

            // Verify the worker process is registered.
            EXPECT_EQ(Process::GetInstance()->GetWorkerPidInfoMap().size(), 1u);
            worker_pid =
                Process::GetInstance()->GetWorkerPidInfoMap().begin()->first;
            // Verify the worker process is a different one.
            ASSERT_NE(worker_pid, getpid());
            // Check the worker process is alive.
            ASSERT_GT(worker_pid, 0);
            EXPECT_EQ(kill(worker_pid, 0), 0);

            // Post a task to disconnect the mojom connection to test whether
            // the worker process exits.
            base::ThreadTaskRunnerHandle::Get()->PostTask(
                FROM_HERE,
                base::BindLambdaForTesting([&]() { recognizer.reset(); }));

            prediction_callback_done = true;
          }));

  // For safety, sets a timeout of 5min. This is just to guarantee the test will
  // not hang.
  bool is_timeout = false;
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, base::BindLambdaForTesting([&]() {
        is_timeout = true;
        runloop.Quit();
      }),
      base::Milliseconds(1000 * 60 * 5));

  runloop.Run();

  // If timeout, the unit test failed.
  ASSERT_FALSE(is_timeout);

  // Fail the test if the worker process can not be reaped.
  ASSERT_TRUE(reap_worker_process_succeeded) << reap_worker_process_fail_reason;
  // Verify the worker process has exited.
  EXPECT_NE(kill(worker_pid, 0), 0);
  // Verify the worker process has been unregistered.
  EXPECT_EQ(Process::GetInstance()->GetWorkerPidInfoMap().size(), 0u);

  EXPECT_TRUE(model_callback_done);
  EXPECT_TRUE(prediction_callback_done);
}

// This tests, on non-supported boards, the `LoadWebPlatformHandwritingModel`
// API does not crash.
TEST(WebPlatformHandwritingModel, NoCrashOnNonsupportedBoards) {
  // Skip if ondevice HWR is supported. We do not need to worry about whether
  // asan is enabled because dlopen will not be called in the test.
  if (ml::HandwritingLibrary::IsHandwritingLibrarySupported()) {
    return;
  }

  // Loads a model.
  base::RunLoop runloop;

  // Sets the process to be control to test multiprocess code.
  // Note that we need to use `kSingleProcessForTest` because the worker
  // process' crash does not fail the unit test.
  Process::GetInstance()->SetTypeForTesting(
      Process::Type::kSingleProcessForTest);

  mojo::Remote<MachineLearningService> ml_service;
  auto ml_service_impl = std::make_unique<MachineLearningServiceImplForTesting>(
      ml_service.BindNewPipeAndPassReceiver());

  // Tries to load a model.
  mojo::Remote<
      chromeos::machine_learning::web_platform::mojom::HandwritingRecognizer>
      recognizer;

  bool model_callback_done = false;
  auto constraint = chromeos::machine_learning::web_platform::mojom::
      HandwritingModelConstraint::New();
  constraint->languages.push_back("en");
  ml_service->LoadWebPlatformHandwritingModel(
      std::move(constraint), recognizer.BindNewPipeAndPassReceiver(),
      base::BindLambdaForTesting([&](const LoadHandwritingModelResult result) {
        ASSERT_EQ(result, LoadHandwritingModelResult::LOAD_MODEL_ERROR);
        model_callback_done = true;
        runloop.Quit();
      }));

  runloop.Run();
  EXPECT_TRUE(model_callback_done);
}

}  // namespace ml
