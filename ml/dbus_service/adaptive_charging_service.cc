// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/dbus_service/adaptive_charging_service.h"

#include <utility>

namespace ml {
namespace {

using ::chromeos::machine_learning::mojom::BuiltinModelId;
using ::chromeos::machine_learning::mojom::TensorPtr;
// TODO(alanlxl): replace with adaptive charging pb config (and BuiltinModelId).
constexpr char kPreprocessorFileName[] =
    "mlservice-model-smart_dim-20190521-preprocessor.pb";

}  // namespace

AdaptiveChargingService::AdaptiveChargingService(
    std::unique_ptr<brillo::dbus_utils::DBusObject> dbus_object)
    : org::chromium::MachineLearning::AdaptiveChargingAdaptor(this),
      dbus_object_(std::move(dbus_object)),
      tf_model_graph_executor_(new TfModelGraphExecutor(
          BuiltinModelId::SMART_DIM_20190521, kPreprocessorFileName)) {}

AdaptiveChargingService::~AdaptiveChargingService() = default;

void AdaptiveChargingService::RegisterAsync(
    const brillo::dbus_utils::AsyncEventSequencer::CompletionAction&
        completion_callback) {
  RegisterWithDBusObject(dbus_object_.get());
  dbus_object_->RegisterAsync(completion_callback);
}

void AdaptiveChargingService::RequestAdaptiveChargingDecision(
    std::unique_ptr<
        brillo::dbus_utils::DBusMethodResponse<bool, std::vector<double>>>
        response,
    const std::string& serialized_example_proto) {
  if (!tf_model_graph_executor_->Ready()) {
    LOG(ERROR) << "TfModelGraphExecutor is not properly initialized.";
    response->Return(false, std::vector<double>());
    return;
  }

  assist_ranker::RankerExample example;
  if (!example.ParseFromString(serialized_example_proto)) {
    LOG(ERROR) << "Failed to parse serialized_example_proto";
    response->Return(false, std::vector<double>());
    return;
  }

  std::vector<TensorPtr> output_tensors;
  if (!tf_model_graph_executor_->Execute(true /*clear_other_features*/,
                                         &example, &output_tensors)) {
    LOG(ERROR) << "TfModelGraphExecutor::Execute failed!";
    response->Return(false, std::vector<double>());
    return;
  }

  // TODO(alanlxl): deal with the output_tensors and return
  response->Return(true, std::vector<double>{4.0, 4.0, 4.0});
}

}  // namespace ml
