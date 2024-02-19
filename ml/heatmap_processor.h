// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ML_HEATMAP_PROCESSOR_H_
#define ML_HEATMAP_PROCESSOR_H_

#include <memory>
#include <string>
#include <vector>

#include <base/no_destructor.h>
#include <base/sequence_checker.h>
#include <mojo/public/cpp/bindings/pending_remote.h>
#include <mojo/public/cpp/bindings/remote.h>

#include "ml/graph_executor_delegate.h"
#include "ml/model_delegate.h"
#include "ml/mojom/heatmap_palm_rejection.mojom.h"

namespace ml {
// A singleton class for heatmap palm rejection service. The class receives
// heatmap data from the touchscreen hidraw device and run a TF model on it to
// detect whether there is a palm on the screen. Then it sends the detection
// results to its client.
class HeatmapProcessor {
 public:
  // Returns a pointer to the singleton of HeatmapProcessor, no caller should
  // take ownership of this pointer.
  static HeatmapProcessor* GetInstance();

  // Starts the heatmap palm rejection service, loads the model according to
  // `config` and gets ready for heatmap data inputs.
  chromeos::machine_learning::mojom::LoadHeatmapPalmRejectionResult Start(
      mojo::PendingRemote<
          chromeos::machine_learning::mojom::HeatmapPalmRejectionClient> client,
      chromeos::machine_learning::mojom::HeatmapPalmRejectionConfigPtr config);

  // Processes `heatmap_data` to decide whether there is a palm.
  virtual void Process(const std::vector<double>& heatmap_data,
                       int height,
                       int width,
                       base::Time timestamp);

 protected:
  HeatmapProcessor();

 private:
  FRIEND_TEST(HeatmapConsumerTest, PushesData);
  friend class base::NoDestructor<HeatmapProcessor>;

  HeatmapProcessor(const HeatmapProcessor&) = delete;
  HeatmapProcessor& operator=(const HeatmapProcessor&) = delete;

  // Reports the palm rejection result to the remote HeatmapPalmRejectionClient.
  void ReportResult(bool is_palm, base::Time timestamp);

  bool ready_ = false;
  double palm_threshold_ = 0.0;

  mojo::Remote<chromeos::machine_learning::mojom::HeatmapPalmRejectionClient>
      client_;
  std::unique_ptr<ModelDelegate> model_delegate_;
  std::unique_ptr<GraphExecutorDelegate> graph_executor_delegate_;

  // Used for guarding `client_`, `model_delegate_` and
  // `graph_executor_delegate_`.
  SEQUENCE_CHECKER(sequence_checker_);
};
}  // namespace ml

#endif  //  ML_HEATMAP_PROCESSOR_H_
