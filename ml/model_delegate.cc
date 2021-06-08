// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/model_delegate.h"

#include <algorithm>
#include <memory>
#include <utility>

#include <base/check.h>
#include <tensorflow/lite/context.h>
#include <tensorflow/lite/delegates/nnapi/nnapi_delegate.h>
#include <tensorflow/lite/interpreter.h>
#include <tensorflow/lite/kernels/register.h>

#include "ml/request_metrics.h"

namespace ml {
namespace {

// Base name for UMA metrics related to CreateGraphExecutor calls
constexpr char kMetricsRequestName[] = "CreateGraphExecutorResult";
}  // namespace

AlignedModelData::AlignedModelData(std::string model_str) {
  if (reinterpret_cast<std::uintptr_t>(model_str.c_str()) % 4 == 0) {
    // `model_str` is aligned. Keep it.
    original_model_str_ = std::make_unique<std::string>(std::move(model_str));
    aligned_copy_ = nullptr;
    aligned_copy_size_ = 0;
  } else {
    // `model_str` is unaligned. Discard it and make an aligned copy.
    aligned_copy_.reset(new char[model_str.size()]);
    std::copy(model_str.begin(), model_str.end(), aligned_copy_.get());
    aligned_copy_size_ = model_str.size();
  }
}

const char* AlignedModelData::data() const {
  return aligned_copy_ ? aligned_copy_.get() : original_model_str_->c_str();
}

size_t AlignedModelData::size() const {
  return aligned_copy_ ? aligned_copy_size_ : original_model_str_->size();
}

AlignedModelData::~AlignedModelData() = default;

ModelDelegate::ModelDelegate(std::map<std::string, int> required_inputs,
                             std::map<std::string, int> required_outputs,
                             std::unique_ptr<tflite::FlatBufferModel> model,
                             std::unique_ptr<AlignedModelData> model_data,
                             const std::string& metrics_model_name)
    : required_inputs_(std::move(required_inputs)),
      required_outputs_(std::move(required_outputs)),
      model_data_(std::move(model_data)),
      model_(std::move(model)),
      metrics_model_name_(metrics_model_name) {}

ModelDelegate::ModelDelegate(std::map<std::string, int> required_inputs,
                             std::map<std::string, int> required_outputs,
                             std::unique_ptr<tflite::FlatBufferModel> model,
                             const std::string& metrics_model_name)
    : ModelDelegate(std::move(required_inputs),
                    std::move(required_outputs),
                    std::move(model),
                    nullptr /*model_data*/,
                    metrics_model_name) {}

CreateGraphExecutorResult ModelDelegate::CreateGraphExecutorDelegate(
    const bool use_nnapi, GraphExecutorDelegate** graph_executor_delegate) {
  DCHECK(!metrics_model_name_.empty());

  RequestMetrics request_metrics(metrics_model_name_, kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  if (model_ == nullptr) {
    LOG(ERROR) << "Null model provided.";
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR;
  }

  // Instantiate interpreter.
  tflite::ops::builtin::BuiltinOpResolver resolver;
  std::unique_ptr<tflite::Interpreter> interpreter;
  const TfLiteStatus resolve_status =
      tflite::InterpreterBuilder(*model_, resolver)(&interpreter);
  if (resolve_status != kTfLiteOk || !interpreter) {
    LOG(ERROR) << "Could not resolve model ops.";
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR);
    return CreateGraphExecutorResult::MODEL_INTERPRETATION_ERROR;
  }

  // If requested, load and apply NNAPI
  if (use_nnapi) {
    TfLiteDelegate* delegate = tflite::NnApiDelegate();
    if (!delegate) {
      LOG(ERROR) << "NNAPI requested but not available.";
      request_metrics.RecordRequestEvent(
          CreateGraphExecutorResult::NNAPI_UNAVAILABLE);
      return CreateGraphExecutorResult::NNAPI_UNAVAILABLE;
    }
    if (interpreter->ModifyGraphWithDelegate(delegate) != kTfLiteOk) {
      LOG(ERROR) << "Could not use NNAPI delegate.";
      request_metrics.RecordRequestEvent(
          CreateGraphExecutorResult::NNAPI_USE_ERROR);
      return CreateGraphExecutorResult::NNAPI_USE_ERROR;
    }
  }

  // Allocate memory for tensors.
  if (interpreter->AllocateTensors() != kTfLiteOk) {
    request_metrics.RecordRequestEvent(
        CreateGraphExecutorResult::MEMORY_ALLOCATION_ERROR);
    return CreateGraphExecutorResult::MEMORY_ALLOCATION_ERROR;
  }

  *graph_executor_delegate =
      new GraphExecutorDelegate(required_inputs_, required_outputs_,
                                std::move(interpreter), metrics_model_name_);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(CreateGraphExecutorResult::OK);
  return CreateGraphExecutorResult::OK;
}

}  // namespace ml
