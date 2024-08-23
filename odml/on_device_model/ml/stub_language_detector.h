// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_
#define ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_

#include <string>
#include <utility>

#include <base/files/file.h>

namespace language_detection {
class LanguageDetectionModel;

struct Prediction {
  Prediction(const std::string& language, float score)
      : language(language), score(score) {}
  Prediction() = delete;
  std::string language;
  float score;

  bool operator<(const Prediction& other) const { return score < other.score; }
};

}  // namespace language_detection

namespace translate {
// A language detection model that will use a TFLite model to determine the
// language of the content of the web page.
class LanguageDetectionModel {
 public:
  explicit LanguageDetectionModel(
      language_detection::LanguageDetectionModel* tflite_model_) {}
  ~LanguageDetectionModel() = default;

  // Updates the language detection model for use by memory-mapping
  // |model_file| used to detect the language of the page.
  void UpdateWithFile(base::File model_file) {}

  // Returns whether |this| is initialized and is available to handle requests
  // to determine the language of the page.
  bool IsAvailable() const { return false; }

  // Determines content page language from Content-Language code and contents.
  // Returns the contents language results in |predicted_language|,
  // |is_prediction_reliable|, and |prediction_reliability_score|.
  std::string DeterminePageLanguage(const std::string& code,
                                    const std::string& html_lang,
                                    const std::u16string& contents,
                                    std::string* predicted_language,
                                    bool* is_prediction_reliable,
                                    float& prediction_reliability_score) const {
    return "";
  }

  language_detection::Prediction DetectLanguage(
      const std::u16string& contents) const {
    return language_detection::Prediction("", 0);
  }

  std::string GetModelVersion() const;

 private:
  // Execute the model on the provided |sampled_str| and return the top language
  // and the models score/confidence in that prediction.
  std::pair<std::string, float> DetectTopLanguage(
      const std::u16string& sampled_str) const {
    return {};
  }

  // The tflite classifier that can determine the language of text.
  const raw_ptr<language_detection::LanguageDetectionModel> tflite_model_;
};

}  // namespace translate

#endif  // ODML_ON_DEVICE_MODEL_ML_STUB_LANGUAGE_DETECTOR_H_
