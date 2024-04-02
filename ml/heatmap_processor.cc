// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/heatmap_processor.h"

#include <map>
#include <string>
#include <utility>

#include <base/logging.h>

#include "ml/request_metrics.h"

using ::chromeos::machine_learning::mojom::ExecuteResult;
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
namespace {
// TFLite graph node names:
constexpr char kInputNodeName[] = "input";
constexpr char kOutputNodeName[] = "output";

// Base name for UMA metrics related to graph execution
constexpr char kMetricsRequestName[] = "ExecuteResult";
}  // namespace

HeatmapProcessor::HeatmapProcessor() = default;

LoadHeatmapPalmRejectionResult HeatmapProcessor::Start(
    mojo::PendingRemote<HeatmapPalmRejectionClient> client,
    HeatmapPalmRejectionConfigPtr config) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  ready_ = false;

  // Attempt to load model.
  std::unique_ptr<tflite::FlatBufferModel> model =
      tflite::FlatBufferModel::BuildFromFile(config->tf_model_path.c_str());
  if (model == nullptr) {
    LOG(ERROR) << "Failed to load model file '" << config->tf_model_path
               << "'.";
    return LoadHeatmapPalmRejectionResult::LOAD_MODEL_ERROR;
  }
  palm_threshold_ = config->palm_threshold;

  model_delegate_ = std::make_unique<ModelDelegate>(
      std::map<std::string, int>({{kInputNodeName, config->input_node}}),
      std::map<std::string, int>({{kOutputNodeName, config->output_node}}),
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

  ready_ = true;
  return LoadHeatmapPalmRejectionResult::OK;
}

void HeatmapProcessor::Process(const std::vector<double>& heatmap_data,
                               int height,
                               int width,
                               base::Time timestamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  if (!ready_) {
    return;
  }

  RequestMetrics request_metrics("HeatmapPalmRejection", kMetricsRequestName);
  request_metrics.StartRecordingPerformanceMetrics();

  base::flat_map<std::string, TensorPtr> inputs;
  auto tensor = Tensor::New();
  tensor->shape = Int64List::New();
  tensor->shape->value = std::vector<int64_t>{1, height, width, 1};
  tensor->data = ValueList::NewFloatList(FloatList::New(heatmap_data));
  inputs.emplace(kInputNodeName, std::move(tensor));
  std::vector<std::string> outputs({kOutputNodeName});

  std::vector<chromeos::machine_learning::mojom::TensorPtr> output_tensors;
  auto result = graph_executor_delegate_->Execute(std::move(inputs), outputs,
                                                  output_tensors);

  request_metrics.FinishRecordingPerformanceMetrics();

  bool is_palm = false;

  if (result == ExecuteResult::OK) {
    auto& output_data = output_tensors[0]->data;
    if (output_data->is_float_list() &&
        output_data->get_float_list()->value.size() == 1) {
      double prediction = output_data->get_float_list()->value[0];
      is_palm = prediction > palm_threshold_;
      request_metrics.RecordRequestEvent(ExecuteResult::OK);
    } else {
      request_metrics.RecordRequestEvent(ExecuteResult::OUTPUT_MISSING_ERROR);
      LOG(ERROR)
          << "Heatmap palm rejection model returns unexpected output data";
    }
  } else {
    request_metrics.RecordRequestEvent(result);
    LOG(ERROR) << "Heatmap palm rejection model execution failed with error "
               << result;
  }

  ReportResult(is_palm, timestamp);
}

void HeatmapProcessor::ReportResult(bool is_palm, base::Time timestamp) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  auto event = chromeos::machine_learning::mojom::HeatmapProcessedEvent::New();
  event->timestamp = timestamp;
  event->is_palm = is_palm;
  client_->OnHeatmapProcessedEvent(std::move(event));
}

HeatmapProcessor* HeatmapProcessor::GetInstance() {
  static base::NoDestructor<HeatmapProcessor> instance;
  return instance.get();
}
}  // namespace ml
