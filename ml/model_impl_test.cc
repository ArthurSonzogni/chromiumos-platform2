// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include <base/bind.h>
#include <base/macros.h>
#include <base/run_loop.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>
#include <mojo/public/cpp/bindings/interface_request.h>
#include <tensorflow/contrib/lite/model.h>

#include "ml/model_impl.h"
#include "ml/mojom/graph_executor.mojom.h"
#include "ml/mojom/model.mojom.h"
#include "ml/tensor_view.h"
#include "ml/test_utils.h"

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::CreateGraphExecutorResult;
using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::GraphExecutorPtr;
using ::chromeos::machine_learning::mojom::Model;
using ::chromeos::machine_learning::mojom::ModelPtr;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::testing::ElementsAre;

class ModelImplTest : public testing::Test {
 protected:
  // Metadata for the example model:
  // A simple model that adds up two tensors. Inputs and outputs are 1x1 float
  // tensors.
  const std::string model_path_ =
      GetTestModelDir() + "mlservice-model-test_add-20180914.tflite";
  const std::map<std::string, int> model_inputs_ = {{"x", 1}, {"y", 2}};
  const std::map<std::string, int> model_outputs_ = {{"z", 0}};
};

// Test loading an invalid model.
TEST_F(ModelImplTest, TestBadModel) {
  // Pass nullptr instead of a valid model.
  ModelPtr model_ptr;
  const ModelImpl model_impl(model_inputs_, model_outputs_, nullptr /*model*/,
                             mojo::MakeRequest(&model_ptr), "TestModel");
  ASSERT_TRUE(model_ptr.is_bound());

  // Ensure that creating a graph executor fails.
  bool callback_done = false;
  GraphExecutorPtr graph_executor_ptr;
  model_ptr->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor_ptr),
      base::Bind(
          [](bool* callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result,
                      CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
            *callback_done = true;
          },
          &callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_done);
}

// Test loading the valid example model.
TEST_F(ModelImplTest, TestExampleModel) {
  // Read the example TF model from disk.
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path_.c_str());
  ASSERT_NE(model.get(), nullptr);

  // Create model object.
  ModelPtr model_ptr;
  const ModelImpl model_impl(model_inputs_, model_outputs_, std::move(model),
                             mojo::MakeRequest(&model_ptr), "TestModel");
  ASSERT_TRUE(model_ptr.is_bound());

  // Create a graph executor.
  bool cge_callback_done = false;
  GraphExecutorPtr graph_executor_ptr;
  model_ptr->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor_ptr),
      base::Bind(
          [](bool* cge_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *cge_callback_done = true;
          },
          &cge_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(cge_callback_done);

  // Construct input/output for graph execution.
  std::unordered_map<std::string, TensorPtr> inputs;
  inputs.emplace("x", NewTensor<double>({1}, {0.5}));
  inputs.emplace("y", NewTensor<double>({1}, {0.25}));
  std::vector<std::string> outputs({"z"});

  // Execute graph.
  bool exe_callback_done = false;
  graph_executor_ptr->Execute(
      std::move(inputs), std::move(outputs),
      base::Bind(
          [](bool* exe_callback_done, const ExecuteResult result,
             base::Optional<std::vector<TensorPtr>> outputs) {
            // Check that the inference succeeded and gives the expected number
            // of outputs.
            EXPECT_EQ(result, ExecuteResult::OK);
            ASSERT_TRUE(outputs.has_value());
            ASSERT_EQ(outputs->size(), 1);

            // Check that the output tensor has the right type and format.
            const TensorView<double> out_tensor((*outputs)[0]);
            EXPECT_TRUE(out_tensor.IsValidType());
            EXPECT_TRUE(out_tensor.IsValidFormat());

            // Check the output tensor has the expected shape and values.
            EXPECT_THAT(out_tensor.GetShape(), ElementsAre(1));
            EXPECT_THAT(out_tensor.GetValues(), ElementsAre(0.75));

            *exe_callback_done = true;
          },
          &exe_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(exe_callback_done);
}

TEST_F(ModelImplTest, TestGraphExecutorCleanup) {
  // Read the example TF model from disk.
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path_.c_str());
  ASSERT_NE(model.get(), nullptr);

  // Create model object.
  ModelPtr model_ptr;
  const ModelImpl model_impl(model_inputs_, model_outputs_, std::move(model),
                             mojo::MakeRequest(&model_ptr), "TestModel");
  ASSERT_TRUE(model_ptr.is_bound());

  // Create one graph executor.
  bool cge1_callback_done = false;
  GraphExecutorPtr graph_executor_1_ptr;
  model_ptr->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor_1_ptr),
      base::Bind(
          [](bool* cge1_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *cge1_callback_done = true;
          },
          &cge1_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(cge1_callback_done);
  ASSERT_TRUE(graph_executor_1_ptr.is_bound());
  ASSERT_EQ(model_impl.num_graph_executors_for_testing(), 1);

  // Create another graph executor.
  bool cge2_callback_done = false;
  GraphExecutorPtr graph_executor_2_ptr;
  model_ptr->CreateGraphExecutor(
      mojo::MakeRequest(&graph_executor_2_ptr),
      base::Bind(
          [](bool* cge2_callback_done, const CreateGraphExecutorResult result) {
            EXPECT_EQ(result, CreateGraphExecutorResult::OK);
            *cge2_callback_done = true;
          },
          &cge2_callback_done));

  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(cge2_callback_done);
  ASSERT_TRUE(graph_executor_2_ptr.is_bound());
  ASSERT_EQ(model_impl.num_graph_executors_for_testing(), 2);

  // Destroy one graph executor.
  graph_executor_1_ptr.reset();
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(graph_executor_2_ptr.is_bound());
  ASSERT_EQ(model_impl.num_graph_executors_for_testing(), 1);

  // Destroy the other graph executor.
  graph_executor_2_ptr.reset();
  base::RunLoop().RunUntilIdle();
  ASSERT_EQ(model_impl.num_graph_executors_for_testing(), 0);
}

}  // namespace
}  // namespace ml
