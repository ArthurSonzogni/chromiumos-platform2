// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_
#define ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_

#include <string_view>

#include "base/files/file.h"
#include "base/memory/ref_counted.h"
#include "base/memory/scoped_refptr.h"
#include "odml/mojom/on_device_model.mojom.h"

namespace ml {

class LanguageDetector : public base::RefCounted<LanguageDetector> {
 public:
  LanguageDetector() = default;

  static scoped_refptr<LanguageDetector> Create(base::File model_file) {
    return base::MakeRefCounted<LanguageDetector>();
  }

  // Performs language detection on `text` and returns the result in a mojom
  // wire structure.
  on_device_model::mojom::LanguageDetectionResultPtr DetectLanguage(
      std::string_view text) {
    return on_device_model::mojom::LanguageDetectionResult::New();
  }

 private:
  friend class base::RefCounted<LanguageDetector>;

  ~LanguageDetector() = default;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_
