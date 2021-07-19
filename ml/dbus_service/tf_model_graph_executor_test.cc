// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/dbus_service/tf_model_graph_executor.h"

#include <vector>

#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/assist_ranker/ranker_example.pb.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"

namespace ml {
namespace {
constexpr char kPreprocessorFileName[] =
    "mlservice-model-smart_dim-20190521-preprocessor.pb";
constexpr char kBadPreprocessorFileName[] = "non-exist.pb";

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::TensorPtr;

using ::testing::DoubleNear;
using ::testing::ElementsAre;
}  // namespace

// Constructs with bad preprocessor config file.
TEST(TfModelGraphExecutorTest, ConstructWithBadPreprocessorConfig) {
  const auto tf_model_graph_executor = TfModelGraphExecutor::CreateForTesting(
      BuiltinModelId::SMART_DIM_20190521, kBadPreprocessorFileName,
      GetTestModelDir());
  EXPECT_FALSE(tf_model_graph_executor->Ready());
}

// Constructs with unsupported BuiltinModelId.
TEST(TfModelGraphExecutorTest, ConstructWithBadModelId) {
  const auto tf_model_graph_executor = TfModelGraphExecutor::CreateForTesting(
      BuiltinModelId::UNSUPPORTED_UNKNOWN, kPreprocessorFileName,
      GetTestModelDir());
  EXPECT_FALSE(tf_model_graph_executor->Ready());
}

// Constructs a valid tf_model_graph_executor with valid model and preprocessor.
TEST(TfModelGraphExecutorTest, ConstructSuccess) {
  const auto tf_model_graph_executor = TfModelGraphExecutor::CreateForTesting(
      BuiltinModelId::SMART_DIM_20190521, kPreprocessorFileName,
      GetTestModelDir());
  EXPECT_TRUE(tf_model_graph_executor->Ready());
}

// Tests that TfModelGraphExecutor works with smart_dim_20190521 assets.
TEST(TfModelGraphExecutorTest, ExecuteSmartDim20190521) {
  const auto tf_model_graph_executor = TfModelGraphExecutor::CreateForTesting(
      BuiltinModelId::SMART_DIM_20190521, kPreprocessorFileName,
      GetTestModelDir());
  ASSERT_TRUE(tf_model_graph_executor->Ready());

  assist_ranker::RankerExample example;
  std::vector<TensorPtr> output_tensors;

  ASSERT_TRUE(tf_model_graph_executor->Execute(true /*clear_other_features*/,
                                               &example, &output_tensors));

  // Check that the output tensor has the right type and format.
  const TensorView<double> out_tensor_view(output_tensors[0]);
  ASSERT_TRUE(out_tensor_view.IsValidType());
  ASSERT_TRUE(out_tensor_view.IsValidFormat());

  // Check the output tensor has the expected shape and values.
  std::vector<int64_t> expected_shape{1L, 1L};
  const double expected_output = -0.625682;
  EXPECT_EQ(out_tensor_view.GetShape(), expected_shape);
  EXPECT_THAT(out_tensor_view.GetValues(),
              ElementsAre(DoubleNear(expected_output, 1e-5)));
}

}  // namespace ml
