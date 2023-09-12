// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <gtest/gtest.h>
#include <set>
#include <string>

#include "quirks/quirks.pb.h"
#include "sommelier-quirks.h"  // NOLINT(build/include_directory)
#include "sommelier-window.h"  // NOLINT(build/include_directory)
#include "testing/x11-test-base.h"

namespace vm_tools::sommelier {

using QuirksTest = X11TestBase;

TEST_F(QuirksTest, ShouldSelectivelyEnableFeatures) {
  sl_window* game123 = CreateToplevelWindow();
  game123->steam_game_id = 123;
  sl_window* game456 = CreateToplevelWindow();
  game456->steam_game_id = 456;
  Quirks quirks;

  quirks.Load(
      "sommelier { \n"
      "  condition { steam_game_id: 123 }\n"
      "  enable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");

  EXPECT_TRUE(quirks.IsEnabled(game123, quirks::FEATURE_X11_MOVE_WINDOWS));
  EXPECT_FALSE(quirks.IsEnabled(game456, quirks::FEATURE_X11_MOVE_WINDOWS));
}

TEST_F(QuirksTest, LaterRulesTakePriority) {
  sl_window* game123 = CreateToplevelWindow();
  game123->steam_game_id = 123;
  Quirks quirks;

  // Act: Load conflicting rules.
  quirks.Load(
      "sommelier { \n"
      "  condition { steam_game_id: 123 }\n"
      "  enable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");
  quirks.Load(
      "sommelier { \n"
      "  condition { steam_game_id: 123 }\n"
      "  disable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");

  // Assert: Feature is disabled, since that rule was last.
  EXPECT_FALSE(quirks.IsEnabled(game123, quirks::FEATURE_X11_MOVE_WINDOWS));

  // Act: Load another rule that enables the feature.
  quirks.Load(
      "sommelier { \n"
      "  condition { steam_game_id: 123 }\n"
      "  enable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");

  // Assert: Feature is now enabled.
  EXPECT_TRUE(quirks.IsEnabled(game123, quirks::FEATURE_X11_MOVE_WINDOWS));
}

TEST_F(QuirksTest, EmptyConditionsAreFalse) {
  sl_window* window = CreateToplevelWindow();
  Quirks quirks;

  quirks.Load(
      "sommelier { \n"
      "  condition { }\n"
      "  enable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");

  EXPECT_FALSE(quirks.IsEnabled(window, quirks::FEATURE_X11_MOVE_WINDOWS));
}

TEST_F(QuirksTest, AllConditionsMustMatch) {
  sl_window* game123 = CreateToplevelWindow();
  game123->steam_game_id = 123;
  Quirks quirks;

  quirks.Load(
      "sommelier { \n"
      "  condition { steam_game_id: 123 }\n"
      "  condition { steam_game_id: 456 }\n"
      "  enable: FEATURE_X11_MOVE_WINDOWS\n"
      "}");

  EXPECT_FALSE(quirks.IsEnabled(game123, quirks::FEATURE_X11_MOVE_WINDOWS));
}

}  // namespace vm_tools::sommelier
