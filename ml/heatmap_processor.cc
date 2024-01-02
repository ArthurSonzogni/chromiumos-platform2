// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/heatmap_processor.h"

#include <map>
#include <string>
#include <utility>

#include <base/logging.h>

using ::chromeos::machine_learning::mojom::FloatList;
using ::chromeos::machine_learning::mojom::GpuDelegateApi;
using ::chromeos::machine_learning::mojom::HeatmapPalmRejectionClient;
using ::chromeos::machine_learning::mojom::HeatmapPalmRejectionConfigPtr;
using ::chromeos::machine_learning::mojom::Int64List;
using ::chromeos::machine_learning::mojom::LoadHeatmapPalmRejectionResult;
using ::chromeos::machine_learning::mojom::Tensor;
using ::chromeos::machine_learning::mojom::TensorPtr;
using ::chromeos::machine_learning::mojom::ValueList;

namespace ml {
HeatmapProcessor::HeatmapProcessor() = default;

LoadHeatmapPalmRejectionResult HeatmapProcessor::Start(
    mojo::PendingRemote<HeatmapPalmRejectionClient> client,
    HeatmapPalmRejectionConfigPtr config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Attempt to load model.
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(config->tf_model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << config->tf_model_path
               << "'.";
    return LoadHeatmapPalmRejectionResult::LOAD_MODEL_ERROR;
  }

  model_delegate_ = std::make_unique<ModelDelegate>(
      std::map<std::string, int>({{"input", config->input_node}}),
      std::map<std::string, int>({{"output", config->output_node}}),
      std::move(model), "PonchoPalmRejectionModel");
  std::unique_ptr<GraphExecutorDelegate> graph_executor_delegate;
  if (model_delegate_->CreateGraphExecutorDelegate(
          false, false, GpuDelegateApi::UNKNOWN, &graph_executor_delegate) !=
      CreateGraphExecutorResult::OK) {
    LOG(ERROR) << "Failed to create graph executor";
    return LoadHeatmapPalmRejectionResult::CREATE_GRAPH_EXECUTOR_ERROR;
  }

  graph_executor_delegate_ = std::move(graph_executor_delegate);

  client_ = mojo::Remote(std::move(client));
  return LoadHeatmapPalmRejectionResult::OK;
}

void HeatmapProcessor::ReportResult(bool is_palm) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto event = chromeos::machine_learning::mojom::HeatmapProcessedEvent::New();
  event->is_palm = is_palm;
  client_->OnHeatmapProcessedEvent(std::move(event));
}

HeatmapProcessor* HeatmapProcessor::GetInstance() {
  static base::NoDestructor<HeatmapProcessor> instance;
  return instance.get();
}
}  // namespace ml
