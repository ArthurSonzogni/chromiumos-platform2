// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/machine_learning_service_impl.h"
#include "ml/request_metrics.h"

#include <memory>
#include <utility>

#include <base/bind.h>
#include <base/bind_helpers.h>
#include <tensorflow/lite/model.h>

#include "ml/model_impl.h"
#include "ml/mojom/model.mojom.h"

namespace ml {

namespace {

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::BuiltinModelSpecPtr;
using ::chromeos::machine_learning::mojom::FlatBufferModelSpecPtr;
using ::chromeos::machine_learning::mojom::LoadModelResult;
using ::chromeos::machine_learning::mojom::ModelId;
using ::chromeos::machine_learning::mojom::ModelRequest;
using ::chromeos::machine_learning::mojom::ModelSpecPtr;

constexpr char kSystemModelDir[] = "/opt/google/chrome/ml_models/";
// Base name for UMA metrics related to LoadModel requests
constexpr char kMetricsRequestName[] = "LoadModelResult";

// To avoid passing a lambda as a base::Closure.
void DeleteModelImpl(const ModelImpl* const model_impl) {
  delete model_impl;
}

}  // namespace

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe,
    base::Closure connection_error_handler,
    const std::string& model_dir)
    : model_metadata_(GetModelMetadata()),
      builtin_model_metadata_(GetBuiltinModelMetadata()),
      model_dir_(model_dir),
      binding_(this, std::move(pipe)) {
  binding_.set_connection_error_handler(std::move(connection_error_handler));
}

MachineLearningServiceImpl::MachineLearningServiceImpl(
    mojo::ScopedMessagePipeHandle pipe, base::Closure connection_error_handler)
    : MachineLearningServiceImpl(std::move(pipe),
                                 std::move(connection_error_handler),
                                 kSystemModelDir) {}

// TODO(crbug.com/990619): Remove this once clients migrate to
// |LoadBuiltinModel|.
void MachineLearningServiceImpl::LoadModel(ModelSpecPtr spec,
                                           ModelRequest request,
                                           const LoadModelCallback& callback) {
  // Unsupported models do not have metadata entries.
  const auto metadata_lookup = model_metadata_.find(spec->id);
  if (metadata_lookup == model_metadata_.end()) {
    LOG(WARNING) << "LoadModel requested for unsupported model ID " << spec->id
                 << ".";
    callback.Run(LoadModelResult::MODEL_SPEC_ERROR);
    RecordModelSpecificationErrorEvent();
    return;
  }

  const ModelMetadata& metadata = metadata_lookup->second;

  DCHECK(!metadata.metrics_model_name.empty());

  RequestMetrics<LoadModelResult> request_metrics(metadata.metrics_model_name,
                                                  kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Attempt to load model.
  const std::string model_path = model_dir_ + metadata.model_file;
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << model_path << "'.";
    callback.Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Use a connection error handler to strongly bind |model_impl| to |request|.
  ModelImpl* const model_impl = new ModelImpl(
      metadata.required_inputs, metadata.required_outputs, std::move(model),
      std::move(request), metadata.metrics_model_name);
  model_impl->set_connection_error_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));
  callback.Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadBuiltinModel(
    BuiltinModelSpecPtr spec,
    ModelRequest request,
    const LoadBuiltinModelCallback& callback) {
  // Unsupported models do not have metadata entries.
  const auto metadata_lookup = builtin_model_metadata_.find(spec->id);
  if (metadata_lookup == builtin_model_metadata_.end()) {
    LOG(WARNING) << "LoadModel requested for unsupported model ID " << spec->id
                 << ".";
    callback.Run(LoadModelResult::MODEL_SPEC_ERROR);
    RecordModelSpecificationErrorEvent();
    return;
  }

  const BuiltinModelMetadata& metadata = metadata_lookup->second;

  DCHECK(!metadata.metrics_model_name.empty());

  RequestMetrics<LoadModelResult> request_metrics(metadata.metrics_model_name,
                                                  kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Attempt to load model.
  const std::string model_path = model_dir_ + metadata.model_file;
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << model_path << "'.";
    callback.Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Use a connection error handler to strongly bind |model_impl| to |request|.
  ModelImpl* const model_impl = new ModelImpl(
      metadata.required_inputs, metadata.required_outputs, std::move(model),
      std::move(request), metadata.metrics_model_name);

  model_impl->set_connection_error_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));
  callback.Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

void MachineLearningServiceImpl::LoadFlatBufferModel(
    FlatBufferModelSpecPtr spec,
    ModelRequest request,
    const LoadFlatBufferModelCallback& callback) {
  DCHECK(!spec->metrics_model_name.empty());

  RequestMetrics<LoadModelResult> request_metrics(spec->metrics_model_name,
                                                  kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  // Take the ownership of the content of |model_string| because |ModelImpl| has
  // to hold the memory.
  auto model_string_impl =
      std::make_unique<std::string>(std::move(spec->model_string));

  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromBuffer(model_string_impl->c_str(),
                                               model_string_impl->length());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model string of metric name: "
               << spec->metrics_model_name << "'.";
    callback.Run(LoadModelResult::LOAD_MODEL_ERROR);
    request_metrics.RecordRequestEvent(LoadModelResult::LOAD_MODEL_ERROR);
    return;
  }

  // Use a connection error handler to strongly bind |model_impl| to |request|.
  ModelImpl* model_impl = new ModelImpl(
      std::map<std::string, int>(spec->inputs.begin(), spec->inputs.end()),
      std::map<std::string, int>(spec->outputs.begin(), spec->outputs.end()),
      std::move(model), std::move(model_string_impl), std::move(request),
      spec->metrics_model_name);

  model_impl->set_connection_error_handler(
      base::Bind(&DeleteModelImpl, base::Unretained(model_impl)));
  callback.Run(LoadModelResult::OK);

  request_metrics.FinishRecordingPerformanceMetrics();
  request_metrics.RecordRequestEvent(LoadModelResult::OK);
}

}  // namespace ml
