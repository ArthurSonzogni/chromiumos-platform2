// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/graph_executor_impl.h"

#include <set>
#include <utility>

#include "ml/tensor_view.h"
#include "mojom/tensor.mojom.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::ExecuteResult;
using ::chromeos::machine_learning::mojom::GraphExecutorRequest;
using ::chromeos::machine_learning::mojom::Int64List;
using ::chromeos::machine_learning::mojom::Tensor;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::ValueList;

// Verifies |tensor| is valid (i.e. is of type |TensorType| and of the correct
// shape for this input) and copies its data into the graph |interpreter| at
// position |index|.
template <typename TensorType, typename MemoryType>
ExecuteResult PopulateInput(const TensorPtr& tensor,
                            const int index,
                            tflite::Interpreter* const interpreter) {
  const TensorView<TensorType> tensor_view(tensor);

  if (!tensor_view.IsValidType())
    return ExecuteResult::INPUT_TYPE_ERROR;

  if (!tensor_view.IsValidFormat())
    return ExecuteResult::INPUT_FORMAT_ERROR;

  // Check that given input shape matches that expected by TF lite.

  const TfLiteIntArray& expected_dims = *interpreter->tensor(index)->dims;
  const mojo::Array<int64_t>& actual_dims = tensor_view.GetShape();

  bool shape_matches = expected_dims.size == actual_dims.size();
  for (int i = 0; shape_matches && i < expected_dims.size; ++i) {
    shape_matches = expected_dims.data[i] == actual_dims[i];
  }

  if (!shape_matches)
    return ExecuteResult::INPUT_SHAPE_ERROR;

  MemoryType* const input_memory = interpreter->typed_tensor<MemoryType>(index);
  const mojo::Array<TensorType>& tensor_values = tensor_view.GetValues();
  for (int i = 0; i < tensor_values.size(); ++i) {
    input_memory[i] = tensor_values[i];
  }

  return ExecuteResult::OK;
}

ExecuteResult InvalidInput(const TensorPtr&, int, tflite::Interpreter*) {
  return ExecuteResult::EXECUTION_ERROR;
}

// A table of functions to validate / populate data for model nodes expecting
// input of each TF lite type.
//
// This table is indexed by TfLiteType, the possible values of which can be
// found at <tensorflow/contrib/lite/context.h>. We make the following
// assumptions about index values:
//   1) They will remain consistent across TF lite releases, and
//   2) They will always start from (close to) 0 and be (mostly) consecutive.
//
// Since TfLiteType is part of the stable C API for TF lite, these assumptions
// seem fair.
constexpr decltype(&InvalidInput) kPopulateInputFns[] = {
    &InvalidInput,                     // kTfLiteNoType
    &PopulateInput<double, float>,     // kTfLiteFloat32
    &PopulateInput<int64_t, int32_t>,  // kTfLiteInt32
    &PopulateInput<int64_t, uint8_t>,  // kTfLiteUInt8
    &PopulateInput<int64_t, int64_t>,  // kTfLiteInt64
    &InvalidInput,                     // kTfLiteString
    &PopulateInput<int64_t, bool>,     // kTfLiteBool
};

// Copies data from position |index| in the graph |interpreter| into the given
// tensor object.
template <typename TensorType, typename MemoryType>
ExecuteResult PopulateOutput(const int index,
                             const tflite::Interpreter& interpreter,
                             const TensorPtr& tensor) {
  TensorView<TensorType> tensor_view(tensor);
  tensor_view.Allocate();

  // Empty output is not valid.
  const TfLiteIntArray& dims = *interpreter.tensor(index)->dims;
  if (dims.size == 0)
    return ExecuteResult::EXECUTION_ERROR;

  // Copy across size information and calculate the number of elements being
  // output.
  int64_t num_entries = 1;
  mojo::Array<int64_t>& tensor_dims = tensor_view.GetShape();
  tensor_dims.resize(dims.size);
  for (int i = 0; i < dims.size; ++i) {
    const int64_t dim_length = dims.data[i];

    if (dim_length <= 0)
      return ExecuteResult::EXECUTION_ERROR;

    tensor_dims[i] = dim_length;
    num_entries *= dim_length;
  }

  // Populate tensor values.
  const MemoryType* const output_memory =
      interpreter.typed_tensor<MemoryType>(index);
  mojo::Array<TensorType>& tensor_values = tensor_view.GetValues();
  tensor_values.resize(num_entries);
  for (int i = 0; i < num_entries; ++i) {
    tensor_values[i] = output_memory[i];
  }

  return ExecuteResult::OK;
}

ExecuteResult InvalidOutput(int, const tflite::Interpreter&, const TensorPtr&) {
  return ExecuteResult::EXECUTION_ERROR;
}

// A table of functions to populate data for tensors from output of each TF lite
// type.
//
// This table is indexed by TfLiteType, the possible values of which can be
// found at <tensorflow/contrib/lite/context.h>. See the caveats discussed in
// the comment above |kPopulateInputFns|.
constexpr decltype(&InvalidOutput) kPopulateOutputFns[] = {
    &InvalidOutput,                     // kTfLiteNoType
    &PopulateOutput<double, float>,     // kTfLiteFloat32
    &PopulateOutput<int64_t, int32_t>,  // kTfLiteInt32
    &PopulateOutput<int64_t, uint8_t>,  // kTfLiteUInt8
    &PopulateOutput<int64_t, int64_t>,  // kTfLiteInt64
    &InvalidOutput,                     // kTfLiteString
    &PopulateOutput<int64_t, bool>,     // kTfLiteBool
};

// For making callback invocations nicer.
mojo::Array<TensorPtr> NullArray() {
  return mojo::Array<TensorPtr>(nullptr);
}

}  // namespace

GraphExecutorImpl::GraphExecutorImpl(
    const std::map<std::string, int>& required_inputs,
    const std::map<std::string, int>& required_outputs,
    std::unique_ptr<tflite::Interpreter> interpreter,
    GraphExecutorRequest request)
    : required_inputs_(required_inputs),
      required_outputs_(required_outputs),
      interpreter_(std::move(interpreter)),
      binding_(this, std::move(request)) {}

void GraphExecutorImpl::set_connection_error_handler(
    base::Closure connection_error_handler) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

void GraphExecutorImpl::Execute(
    const mojo::Map<mojo::String, TensorPtr> tensors,
    const mojo::Array<mojo::String> outputs,
    const ExecuteCallback& callback) {
  // Validate input and output names (before executing graph, for efficiency).

  for (const auto& kv : tensors) {
    const std::string& cur_input_name = kv.first.get();

    const auto name_lookup = required_inputs_.find(cur_input_name);
    if (name_lookup == required_inputs_.end() ||
        name_lookup->second >= interpreter_->tensors_size()) {
      callback.Run(ExecuteResult::UNKNOWN_INPUT_ERROR, NullArray());
      return;
    }
  }
  if (tensors.size() != required_inputs_.size()) {
    callback.Run(ExecuteResult::INPUT_MISSING_ERROR, NullArray());
    return;
  }

  std::set<std::string> seen_outputs;
  for (const mojo::String& cur_output_name : outputs) {
    const auto name_lookup = required_outputs_.find(cur_output_name.get());
    if (name_lookup == required_outputs_.end() ||
        name_lookup->second >= interpreter_->tensors_size()) {
      callback.Run(ExecuteResult::UNKNOWN_OUTPUT_ERROR, NullArray());
      return;
    }

    // Specifying the same output twice is an error.
    const auto insert_result = seen_outputs.insert(cur_output_name.get());
    if (!insert_result.second) {
      callback.Run(ExecuteResult::DUPLICATE_OUTPUT_ERROR, NullArray());
      return;
    }
  }
  if (outputs.size() != required_outputs_.size()) {
    callback.Run(ExecuteResult::OUTPUT_MISSING_ERROR, NullArray());
    return;
  }

  // Copy input data into the interpreter.
  for (const auto& kv : tensors) {
    const std::string& cur_input_name = kv.first.get();
    const TensorPtr& cur_input = kv.second;

    // Always valid, by the input name check at the start of this function.
    const int cur_input_id = required_inputs_.find(cur_input_name)->second;

    // Check that the current input node is a supported type.
    const uint32_t cur_input_type = interpreter_->tensor(cur_input_id)->type;
    if (cur_input_type >= arraysize(kPopulateInputFns)) {
      LOG(ERROR) << "TF lite graph contains invalid input node " << cur_input_id
                 << " of type " << cur_input_type << ".";
      callback.Run(ExecuteResult::EXECUTION_ERROR, NullArray());
      return;
    }

    // Attempt to copy input data into the current input node.
    const ExecuteResult populate_input_result =
        (*kPopulateInputFns[cur_input_type])(cur_input, cur_input_id,
                                             interpreter_.get());
    if (populate_input_result != ExecuteResult::OK) {
      callback.Run(populate_input_result, NullArray());
      return;
    }
  }

  // Execute graph.
  if (interpreter_->Invoke() != kTfLiteOk) {
    LOG(ERROR) << "TF lite graph execution failed unexpectedly.";
    callback.Run(ExecuteResult::EXECUTION_ERROR, NullArray());
    return;
  }

  // Extract output.
  mojo::Array<chromeos::machine_learning::mojom::TensorPtr> output_tensors;
  for (const mojo::String& cur_output_name : outputs) {
    output_tensors.push_back(Tensor::New());

    // Always valid, by the output name check at the start of this function.
    const int cur_output_id =
        required_outputs_.find(cur_output_name.get())->second;

    // Check that the current output node is a supported type.
    const uint32_t cur_output_type = interpreter_->tensor(cur_output_id)->type;
    if (cur_output_type >= arraysize(kPopulateOutputFns)) {
      LOG(ERROR) << "TF lite graph contains invalid output node "
                 << cur_output_id << " of type " << cur_output_type << ".";
      callback.Run(ExecuteResult::EXECUTION_ERROR, NullArray());
      return;
    }

    // Attempt to extract data from the current output node.
    const ExecuteResult populate_output_result =
        (*kPopulateOutputFns[cur_output_type])(cur_output_id, *interpreter_,
                                               *--output_tensors.end());
    if (populate_output_result != ExecuteResult::OK) {
      callback.Run(populate_output_result, NullArray());
      return;
    }
  }

  callback.Run(ExecuteResult::OK, std::move(output_tensors));
}

}  // namespace ml
