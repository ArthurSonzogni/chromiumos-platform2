// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_MANTIS_PROCESSOR_H_
#define ODML_MANTIS_PROCESSOR_H_

#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <base/memory/raw_ref.h>
#include <base/memory/scoped_refptr.h>
#include <base/memory/weak_ptr.h>
#include <base/task/sequenced_task_runner.h>
#include <base/task/task_runner.h>
#include <base/task/task_traits.h>
#include <base/task/thread_pool.h>
#include <chromeos/mojo/service_constants.h>
#include <metrics/metrics_library.h>
#include <mojo/public/cpp/bindings/receiver.h>
#include <mojo/public/cpp/bindings/receiver_set.h>

#include "odml/cros_safety/safety_service_manager.h"
#include "odml/mantis/lib_api.h"
#include "odml/mantis/metrics.h"
#include "odml/mojom/mantis_processor.mojom.h"
#include "odml/mojom/mantis_service.mojom.h"
#include "odml/utils/performance_timer.h"

namespace mantis {

enum class ImageType {
  kInputImage,
  kOutputImage,
  kGeneratedRegion,
  kGeneratedRegionOutpaintng,
};

enum class OperationType {
  kGenfill,
  kInpainting,
  kOutpainting,
};
struct ProcessFuncResult {
  std::optional<mojom::MantisError> error;
  std::vector<uint8_t> image;
  std::vector<uint8_t> generated_region;
};
// TODO(b/375929152): Use NoDefault for required args.
struct MantisProcess {
  const std::vector<uint8_t> image;
  const std::vector<uint8_t> mask;
  uint32_t seed;
  std::optional<std::string> prompt;
  const OperationType operation_type;
  base::OnceCallback<void(mojom::MantisResultPtr)> callback;
  base::OnceCallback<ProcessFuncResult()> process_func;
  // Metric info to be used on main thread.
  mantis::TimeMetric time_metric;
  mantis::ImageGenerationType generated_image_type_metric;
  odml::PerformanceTimer::Ptr timer;
  // Might not be populated
  std::vector<uint8_t> image_result;
  std::vector<uint8_t> generated_region;
};

class MantisProcessor : public mojom::MantisProcessor {
 public:
  explicit MantisProcessor(
      raw_ref<MetricsLibraryInterface> metrics_lib,
      scoped_refptr<base::SequencedTaskRunner> mantis_api_runner,
      MantisComponent component,
      const MantisAPI* api,
      mojo::PendingReceiver<mojom::MantisProcessor> receiver,
      raw_ref<cros_safety::SafetyServiceManager> safety_service_manager,
      base::OnceCallback<void()> on_disconnected,
      base::OnceCallback<void(mantis::mojom::InitializeResult)> callback);

  ~MantisProcessor();

  MantisProcessor(const MantisProcessor&) = delete;
  MantisProcessor& operator=(const MantisProcessor&) = delete;

  void AddReceiver(mojo::PendingReceiver<mojom::MantisProcessor> receiver) {
    receiver_set_.Add(this, std::move(receiver),
                      base::SequencedTaskRunner::GetCurrentDefault());
  }

  void Inpainting(const std::vector<uint8_t>& image,
                  const std::vector<uint8_t>& mask,
                  uint32_t seed,
                  InpaintingCallback callback) override;

  void Outpainting(const std::vector<uint8_t>& image,
                   const std::vector<uint8_t>& mask,
                   uint32_t seed,
                   OutpaintingCallback callback) override;

  void GenerativeFill(const std::vector<uint8_t>& image,
                      const std::vector<uint8_t>& mask,
                      uint32_t seed,
                      const std::string& prompt,
                      GenerativeFillCallback callback) override;

  void Segmentation(const std::vector<uint8_t>& image,
                    const std::vector<uint8_t>& prior,
                    SegmentationCallback callback) override;

  void ClassifyImageSafety(const std::vector<uint8_t>& image,
                           ClassifyImageSafetyCallback callback) override;

 protected:
  virtual void OnClassifyImageOutputDone(
      std::unique_ptr<MantisProcess> process,
      std::vector<mojom::SafetyClassifierVerdict> results);

 private:
  void ClassifyImageSafetyInternal(
      const std::vector<uint8_t>& image,
      const std::string& text,
      ImageType image_type,
      base::OnceCallback<void(mojom::SafetyClassifierVerdict)> callback);

  void OnDisconnected();

  void OnSegmentationDone(SegmentationCallback callback,
                          odml::PerformanceTimer::Ptr timer,
                          const SegmentationResult& lib_result);

  void ProcessImage(std::unique_ptr<MantisProcess> process);

  void OnProcessDone(std::unique_ptr<MantisProcess> process,
                     const ProcessFuncResult& lib_result);

  const raw_ref<MetricsLibraryInterface> metrics_lib_;

  const scoped_refptr<base::SequencedTaskRunner> mantis_api_runner_;

  MantisComponent component_;

  const raw_ptr<const MantisAPI> api_;

  raw_ref<cros_safety::SafetyServiceManager> safety_service_manager_;

  mojo::ReceiverSet<mojom::MantisProcessor> receiver_set_;

  base::WeakPtrFactory<MantisProcessor> weak_ptr_factory_{this};

  base::OnceClosure on_disconnected_;
};

}  // namespace mantis

#endif  // ODML_MANTIS_PROCESSOR_H_
