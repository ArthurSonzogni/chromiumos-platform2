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

TEST(TextSuggestionsText, ExampleRequest) {
  auto* const instance = ml::TextSuggestions::GetInstance();
  if (instance->GetStatus() == ml::TextSuggestions::Status::kNotSupported) {
    return;
  }

  ASSERT_EQ(instance->GetStatus(), TextSuggestions::Status::kOk);

  TextSuggester const suggester = instance->CreateTextSuggester();
  instance->LoadTextSuggester(suggester);

  chrome_knowledge::TextSuggesterRequest request;
  request.set_text("How are y");

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

}  // namespace
}  // namespace ml
