// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <fstream>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <gtest/gtest.h>

#include "chrome/knowledge/handwriting/handwriting_validate.pb.h"
#include "ml/handwriting.h"
#include "ml/util.h"

namespace ml {
namespace {

using chromeos::machine_learning::mojom::HandwritingRecognizerSpec;

constexpr char kLabeledRequestPathEn[] =
    "/build/share/libhandwriting/handwriting_labeled_requests.pb";
constexpr char kLabeledRequestPathGesture[] =
    "/build/share/libhandwriting/gesture_labeled_requests.pb";

}  // namespace

TEST(HandwritingLibraryTest, CanLoadLibrary) {
  auto* const instance = ml::HandwritingLibrary::GetInstance();
  if (IsAsan()) {
    EXPECT_FALSE(ml::HandwritingLibrary::IsHandwritingLibrarySupported());
    EXPECT_FALSE(
        ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported());
    EXPECT_EQ(instance->GetStatus(),
              ml::HandwritingLibrary::Status::kNotSupported);
    return;
  }

  if (ml::HandwritingLibrary::IsUseLibHandwritingEnabled()) {
    EXPECT_TRUE(ml::HandwritingLibrary::IsHandwritingLibrarySupported());
    EXPECT_TRUE(
        ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported());
    EXPECT_EQ(instance->GetStatus(), ml::HandwritingLibrary::Status::kOk);
    return;
  }

  if (ml::HandwritingLibrary::IsUseLibHandwritingDlcEnabled()) {
    EXPECT_TRUE(ml::HandwritingLibrary::IsHandwritingLibrarySupported());
    EXPECT_FALSE(
        ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported());
    EXPECT_EQ(instance->GetStatus(),
              ml::HandwritingLibrary::Status::kLoadLibraryFailed);
    return;
  }
}

// Tests each supported language against a file of labeled requests.
TEST(HandwritingLibraryTest, ExampleRequest) {
  // Nothing to test on an unsupported platform.
  if (!ml::HandwritingLibrary::IsHandwritingLibraryUnitTestSupported()) {
    return;
  }

  auto* const instance = ml::HandwritingLibrary::GetInstance();
  ASSERT_EQ(instance->GetStatus(), ml::HandwritingLibrary::Status::kOk);

  const std::vector<std::string> languages = {"en", "gesture_in_context"};
  const std::vector<std::string> labeled_request_paths = {
      kLabeledRequestPathEn, kLabeledRequestPathGesture};

  for (int i = 0; i < languages.size(); ++i) {
    HandwritingRecognizer const recognizer =
        instance->CreateHandwritingRecognizer();
    ASSERT_TRUE(instance->LoadHandwritingRecognizer(recognizer, languages[i]));

    chrome_knowledge::HandwritingRecognizerLabeledRequests test_data;
    std::string buf;
    ASSERT_TRUE(
        base::ReadFileToString(base::FilePath(labeled_request_paths[i]), &buf));
    ASSERT_TRUE(test_data.ParseFromString(buf));
    ASSERT_GT(test_data.labeled_requests().size(), 0);
    for (auto const& request : test_data.labeled_requests()) {
      chrome_knowledge::HandwritingRecognizerResult result;
      ASSERT_TRUE(instance->RecognizeHandwriting(recognizer, request.request(),
                                                 &result));
      ASSERT_GT(result.candidates().size(), 0);
      EXPECT_EQ(result.candidates(0).text(), request.label());
    }
    instance->DestroyHandwritingRecognizer(recognizer);
  }
}

}  // namespace ml
