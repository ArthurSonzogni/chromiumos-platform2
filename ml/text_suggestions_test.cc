// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>

#include "ml/text_suggestions.h"
#include "ml/util.h"

namespace ml {
namespace {

TEST(TextSuggestionsTest, CanLoadLibrary) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (IsAsan()) {
    EXPECT_FALSE(ml::TextSuggestions::IsTextSuggestionsSupported());
    EXPECT_EQ(instance->GetStatus(),
              ml::TextSuggestions::Status::kNotSupported);
    return;
  }

  if (ml::TextSuggestions::IsTextSuggestionsSupported()) {
    EXPECT_EQ(instance->GetStatus(), ml::TextSuggestions::Status::kOk);
  } else {
    EXPECT_EQ(instance->GetStatus(),
              ml::TextSuggestions::Status::kNotSupported);
  }
}

TEST(TextSuggestionsText, ExampleCompletionRequestWithDefaultSettings) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(
      suggester,
      chrome_knowledge::MultiWordExperiment::MULTI_WORD_EXPERIMENT_UNSPECIFIED);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("How are y");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_COMPLETION);

  chrome_knowledge::NextWordCompletionCandidate* candidate =
      request.add_next_word_candidates();
  candidate->set_text("you");
  candidate->set_normalized_score(-1.0f);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  EXPECT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "you doing");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.680989f);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText, ExamplePredictionRequestWithDefaultSettings) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(
      suggester,
      chrome_knowledge::MultiWordExperiment::MULTI_WORD_EXPERIMENT_UNSPECIFIED);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("How are ");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_PREDICTION);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  ASSERT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "you doing");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.8141749f);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText,
     GboardExperimentGroupIsSetAndDoesntTriggerForDefaultExample) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(
      suggester,
      chrome_knowledge::MultiWordExperiment::MULTI_WORD_EXPERIMENT_GBOARD);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("How are ");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_PREDICTION);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_EQ(result.candidates_size(), 0);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText,
     ExperimentGboardGroupIsSetAndTriggersExpectedSuggestions) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(
      suggester,
      chrome_knowledge::MultiWordExperiment::MULTI_WORD_EXPERIMENT_GBOARD);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("why a");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_COMPLETION);

  chrome_knowledge::NextWordCompletionCandidate* candidate =
      request.add_next_word_candidates();
  candidate->set_text("aren\'t");
  candidate->set_normalized_score(-1.0f);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  EXPECT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "aren\'t you");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.13418171f);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText,
     ExperimentGboardRelaxedGroupAIsSetAndTriggersExpectedSuggestions) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(suggester,
                              chrome_knowledge::MultiWordExperiment::
                                  MULTI_WORD_EXPERIMENT_GBOARD_RELAXED_A);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("why a");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_COMPLETION);

  chrome_knowledge::NextWordCompletionCandidate* candidate =
      request.add_next_word_candidates();
  candidate->set_text("aren\'t");
  candidate->set_normalized_score(-1.0f);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  EXPECT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "aren\'t you");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.13418171f);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText,
     ExperimentGboardRelaxedGroupBIsSetAndTriggersExpectedSuggestions) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(suggester,
                              chrome_knowledge::MultiWordExperiment::
                                  MULTI_WORD_EXPERIMENT_GBOARD_RELAXED_B);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("I need to double check some details in t");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_COMPLETION);

  chrome_knowledge::NextWordCompletionCandidate* candidate =
      request.add_next_word_candidates();
  candidate->set_text("the");
  candidate->set_normalized_score(-1.0f);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  EXPECT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "the morning");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.5560128f);

  instance->DestroyTextSuggester(suggester);
}

TEST(TextSuggestionsText,
     ExperimentGboardRelaxedGroupCIsSetAndTriggersExpectedSuggestions) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(suggester,
                              chrome_knowledge::MultiWordExperiment::
                                  MULTI_WORD_EXPERIMENT_GBOARD_RELAXED_C);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("I need to double check some details in t");
  request.set_suggestion_mode(
      chrome_knowledge::RequestSuggestionMode::SUGGESTION_MODE_COMPLETION);

  chrome_knowledge::NextWordCompletionCandidate* candidate =
      request.add_next_word_candidates();
  candidate->set_text("the");
  candidate->set_normalized_score(-1.0f);

  chrome_knowledge::TextSuggesterResult result;
  instance->GenerateSuggestions(suggester, request, &result);

  ASSERT_GT(result.candidates_size(), 0);
  EXPECT_EQ(result.candidates(0).has_multi_word(), true);
  EXPECT_EQ(result.candidates(0).multi_word().text(), "the morning");
  EXPECT_FLOAT_EQ(result.candidates(0).multi_word().normalized_score(),
                  -0.5560128f);

  instance->DestroyTextSuggester(suggester);
}

}  // namespace
}  // namespace ml
