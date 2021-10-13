// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "ml/text_suggestions.h"

#include <string>

#include <base/files/file_path.h>
#include <base/logging.h>
#include <base/native_library.h>

namespace ml {
namespace {

constexpr char kTextSuggesterFilesPath[] =
    "/opt/google/chrome/ml_models/suggest/";
constexpr char kTextSuggesterLibraryRelativePath[] = "libsuggest.so";
constexpr char kTextSuggesterModelRelativePath[] = "nwp.uint8.mmap.tflite";
constexpr char kTextSuggesterSymbolsRelativePath[] = "nwp.csym";

}  // namespace

TextSuggestions::TextSuggestions()
    : status_(Status::kUninitialized),
      create_text_suggester_(nullptr),
      load_text_suggester_(nullptr),
      suggest_candidates_(nullptr),
      delete_suggest_candidates_result_data_(nullptr),
      destroy_text_suggester_(nullptr) {
  if (!IsTextSuggestionsSupported()) {
    status_ = Status::kNotSupported;
    return;
  }

  base::NativeLibraryOptions native_library_options;
  native_library_options.prefer_own_symbols = true;
  base::NativeLibraryLoadError error;
  library_.emplace(base::LoadNativeLibraryWithOptions(
      base::FilePath(kTextSuggesterFilesPath)
          .Append(kTextSuggesterLibraryRelativePath),
      native_library_options, &error));
  if (!library_->is_valid()) {
    status_ = Status::kLoadLibraryFailed;
    LOG(ERROR) << error.ToString();
    return;
  }

#define ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(function_ptr, name)          \
  function_ptr =                                                       \
      reinterpret_cast<name##Fn>(library_->GetFunctionPointer(#name)); \
  if (function_ptr == NULL) {                                          \
    status_ = Status::kFunctionLookupFailed;                           \
    return;                                                            \
  }
  // Lookup the function pointers
  ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(create_text_suggester_,
                                    CreateTextSuggester);
  ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(load_text_suggester_, LoadTextSuggester);
  ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(suggest_candidates_, SuggestCandidates);
  ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(delete_suggest_candidates_result_data_,
                                    DeleteSuggestCandidatesResultData);
  ML_TEXT_SUGGESTER_LOOKUP_FUNCTION(destroy_text_suggester_,
                                    DestroyTextSuggester);
#undef ML_TEXT_SUGGESTER_LOOKUP_FUNCTION

  status_ = Status::kOk;
}

TextSuggestions::~TextSuggestions() = default;

TextSuggestions* TextSuggestions::GetInstance() {
  static base::NoDestructor<TextSuggestions> instance;
  return instance.get();
}

TextSuggestions::Status TextSuggestions::GetStatus() const {
  return status_;
}

TextSuggester TextSuggestions::CreateTextSuggester() const {
  DCHECK(status_ == Status::kOk);
  return (*create_text_suggester_)();
}

bool TextSuggestions::LoadTextSuggester(
    TextSuggester const suggester,
    const chrome_knowledge::MultiWordExperiment& experiment) const {
  DCHECK(status_ == Status::kOk);
  chrome_knowledge::TextSuggesterSettings settings;

  chrome_knowledge::MultiWordSettings* multi_word_settings =
      settings.mutable_multi_word_settings();
  multi_word_settings->set_model_path(
      base::FilePath(kTextSuggesterFilesPath)
          .Append(kTextSuggesterModelRelativePath)
          .value());
  multi_word_settings->set_syms_path(
      base::FilePath(kTextSuggesterFilesPath)
          .Append(kTextSuggesterSymbolsRelativePath)
          .value());

  chrome_knowledge::FeatureSettings* feature_settings =
      settings.mutable_feature_settings();
  feature_settings->set_multi_word_enabled(true);
  feature_settings->set_emojis_enabled(false);

  chrome_knowledge::ExperimentSettings* experiment_settings =
      settings.mutable_experiment_settings();
  experiment_settings->set_multi_word(experiment);

  const std::string settings_pb = settings.SerializeAsString();
  return (*load_text_suggester_)(suggester, settings_pb.data(),
                                 settings_pb.size());
}

bool TextSuggestions::GenerateSuggestions(
    TextSuggester const suggester,
    const chrome_knowledge::TextSuggesterRequest& request,
    chrome_knowledge::TextSuggesterResult* const result) const {
  DCHECK(status_ == Status::kOk);
  const std::string request_pb = request.SerializeAsString();
  char* result_data = nullptr;
  int result_size = 0;
  const bool suggestions_generated =
      (*suggest_candidates_)(suggester, request_pb.data(), request_pb.size(),
                             &result_data, &result_size);
  if (suggestions_generated) {
    const bool parse_result_status =
        result->ParseFromArray(result_data, result_size);
    DCHECK(parse_result_status);
    // only need to delete result_data if succeeds.
    (*delete_suggest_candidates_result_data_)(result_data);
  }

  return suggestions_generated;
}

void TextSuggestions::DestroyTextSuggester(
    TextSuggester const suggester) const {
  DCHECK(status_ == Status::kOk);
  (*destroy_text_suggester_)(suggester);
}

}  // namespace ml
