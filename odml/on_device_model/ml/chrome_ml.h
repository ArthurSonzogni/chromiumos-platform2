// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_CHROME_ML_H_
#define ODML_ON_DEVICE_MODEL_ML_CHROME_ML_H_

#include <memory>

#include <base/memory/raw_ptr.h>
#include <base/memory/raw_ref.h>
#include <metrics/metrics_library.h>

#include "odml/on_device_model/ml/chrome_ml_api.h"
#include "odml/utils/odml_shim_loader.h"

namespace ml {

// A ChromeML object encapsulates a reference to the ChromeML library, exposing
// the library's API functions to callers and ensuring that the library remains
// loaded and usable throughout the object's lifetime.
class ChromeML {
 public:
  // Use Get() to acquire a global instance.
  ChromeML(raw_ref<MetricsLibraryInterface> metrics, const ChromeMLAPI* api);
  ~ChromeML();

  // Gets a lazily initialized global instance of ChromeML. May return null
  // if the underlying library could not be loaded.
  static ChromeML* Get(raw_ref<MetricsLibraryInterface> metrics,
                       raw_ref<odml::OdmlShimLoader> shim_loader);

  // Exposes the raw ChromeMLAPI functions defined by the library.
  const ChromeMLAPI& api() const { return *api_; }

 private:
  static std::unique_ptr<ChromeML> Create(
      raw_ref<MetricsLibraryInterface> metrics,
      raw_ref<odml::OdmlShimLoader> shim_loader);

  const raw_ptr<const ChromeMLAPI> api_;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_CHROME_ML_H_
