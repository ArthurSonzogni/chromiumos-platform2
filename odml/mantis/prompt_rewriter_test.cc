// Copyright 2025 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "odml/mantis/prompt_rewriter.h"

#include <gtest/gtest.h>

namespace mantis {
namespace {

class MantisPromptRewriterTest : public testing::Test {};

TEST_F(MantisPromptRewriterTest, ReplaceVerbPattern) {
  EXPECT_EQ(
      RewritePromptForGenerativeFill("Please exchange the cat for the dog"),
      "the dog");
  EXPECT_EQ(RewritePromptForGenerativeFill("Put a fluffy cat on top the house"),
            "a fluffy cat");
}

TEST_F(MantisPromptRewriterTest, PromptTriggerStopword) {
  EXPECT_EQ(RewritePromptForGenerativeFill("remove the car"), "");
  EXPECT_EQ(RewritePromptForGenerativeFill("please erase the house"), "");
}

TEST_F(MantisPromptRewriterTest, ExtractAfterAdditionVerb) {
  EXPECT_EQ(RewritePromptForGenerativeFill("generate a lemon tree"),
            "a lemon tree");
  EXPECT_EQ(RewritePromptForGenerativeFill("fill with a blue airplane"),
            "a blue airplane");
}

TEST_F(MantisPromptRewriterTest, PromptUnchanged) {
  EXPECT_EQ(RewritePromptForGenerativeFill("a cute puppy"), "a cute puppy");
  EXPECT_EQ(RewritePromptForGenerativeFill("three cups of coffee"),
            "three cups of coffee");
}

}  // namespace
}  // namespace mantis
