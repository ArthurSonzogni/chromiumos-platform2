// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_LANGUAGE_DETECTOR_H_
#define ODML_I18N_LANGUAGE_DETECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback.h>

namespace on_device_model {

class LanguageDetector {
 public:
  struct TextLanguage {
    // The BCP-47 language code like "en", "fr", "zh" etc.
    std::string locale;
    // The confidence score of the language detected (range: 0~1).
    float confidence;
    bool operator==(const TextLanguage&) const = default;
  };
  virtual ~LanguageDetector() = default;

  // Whether the language detector is available. When false, Classify will
  // always return nullopt.
  virtual bool IsAvailable() = 0;

  // Return a list of possible languages of `text`, with decreasing confidence.
  // The maximum entries of the vector is determined internally. Returns nullopt
  // if running classification itself fails. Note that even if the
  // classification run succeeds, an empty vector is a valid result from the
  // detector.
  virtual void Classify(
      const std::string& text,
      base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)>
          callback) = 0;
};

}  // namespace on_device_model

#endif  // ODML_I18N_LANGUAGE_DETECTOR_H_
