// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/embedding_model/model_factory.h"

#include <memory>
#include <utility>

#include "odml/embedding_model/model_runner.h"
#include "odml/embedding_model/odml_shim_tokenizer.h"
#include "odml/embedding_model/tflite_model_runner.h"
#include "odml/utils/performance_timer.h"

namespace embedding_model {

constexpr char kLoadDlcTimeHistogramName[] =
    "OnDeviceModel.Embedding.LoadDlcTime";

ModelFactoryImpl::ModelFactoryImpl(
    const raw_ref<odml::OdmlShimLoader> shim_loader,
    const raw_ref<MetricsLibraryInterface> metrics)
    : dlc_model_loader_(metrics),
      shim_loader_(shim_loader),
      metrics_(metrics) {}

std::unique_ptr<ModelRunner> ModelFactoryImpl::BuildRunnerFromInfo(
    ModelInfo&& info) {
  if (info.model_type == kEmbeddingTfliteModelType) {
    auto tokenizer = std::make_unique<OdmlShimTokenizer>(raw_ref(shim_loader_));
    std::unique_ptr<TfliteModelRunner> result =
        std::make_unique<TfliteModelRunner>(
            std::move(info), std::move(tokenizer), shim_loader_, metrics_);
    return result;
  }
  return nullptr;
}

void ModelFactoryImpl::BuildRunnerFromUuid(
    const base::Uuid& uuid, BuildRunnerFromUuidCallback callback) {
  // Note that base::Unretained(this) is safe because dlc_model_loader_ is owned
  // by the model factory.
  auto timer = odml::PerformanceTimer::Create();
  dlc_model_loader_.LoadDlcWithUuid(
      uuid,
      base::BindOnce(&ModelFactoryImpl::OnDlcLoadFinish, base::Unretained(this),
                     std::move(callback), std::move(timer)));
}

void ModelFactoryImpl::OnDlcLoadFinish(
    BuildRunnerFromUuidCallback callback,
    odml::PerformanceTimer::Ptr timer,
    std::optional<struct ModelInfo> model_info) {
  if (!model_info.has_value()) {
    // Load failed, and DlcModelLoader should emit the relevant messages.
    std::move(callback).Run(nullptr);
    return;
  }

  metrics_->SendTimeToUMA(
      /*name=*/kLoadDlcTimeHistogramName, /*sample=*/timer->GetDuration(),
      /*min=*/base::Milliseconds(1), /*max=*/base::Minutes(30), /*buckets=*/50);

  std::unique_ptr<ModelRunner> runner =
      BuildRunnerFromInfo(std::move(*model_info));
  std::move(callback).Run(std::move(runner));
}

}  // namespace embedding_model
