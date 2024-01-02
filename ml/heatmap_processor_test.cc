// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include <gtest/gtest.h>

#include "ml/heatmap_processor.h"
#include "mojo/public/cpp/bindings/receiver.h"

using ::chromeos::machine_learning::mojom::HeatmapPalmRejectionClient;
using ::chromeos::machine_learning::mojom::HeatmapPalmRejectionConfig;
using ::chromeos::machine_learning::mojom::HeatmapProcessedEventPtr;
using ::chromeos::machine_learning::mojom::LoadHeatmapPalmRejectionResult;

namespace ml {
namespace {
constexpr char kModelPath[] =
    "/opt/google/chrome/ml_models/"
    "mlservice-model-poncho_palm_rejection-20230907-v0.tflite";

class FakeClient : public HeatmapPalmRejectionClient {
 public:
  void OnHeatmapProcessedEvent(HeatmapProcessedEventPtr event) override {}
};
}  // namespace

TEST(HeatmapProcessorTest, CanStartService) {
  FakeClient client;
  mojo::Receiver<HeatmapPalmRejectionClient> receiver(&client);
  auto config = HeatmapPalmRejectionConfig::New();
  config->tf_model_path = kModelPath;
  auto* const instance = ml::HeatmapProcessor::GetInstance();
  auto result =
      instance->Start(receiver.BindNewPipeAndPassRemote(), std::move(config));
  EXPECT_EQ(result, LoadHeatmapPalmRejectionResult::OK);
}

}  // namespace ml
