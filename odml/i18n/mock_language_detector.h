// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_I18N_MOCK_LANGUAGE_DETECTOR_H_
#define ODML_I18N_MOCK_LANGUAGE_DETECTOR_H_

#include <optional>
#include <string>
#include <vector>

#include <base/functional/callback.h>
#include <gmock/gmock.h>

#include "odml/i18n/language_detector.h"

namespace on_device_model {

class MockLanguageDetector : public LanguageDetector {
 public:
  MockLanguageDetector() = default;
  MOCK_METHOD(bool, IsAvailable, (), (override));
  MOCK_METHOD(
      void,
      Classify,
      (const std::string& text,
       base::OnceCallback<void(std::optional<std::vector<TextLanguage>>)>
           callback),
      (override));
};

}  // namespace on_device_model

#endif  // ODML_I18N_MOCK_LANGUAGE_DETECTOR_H_
