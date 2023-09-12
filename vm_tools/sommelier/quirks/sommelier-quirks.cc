// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "builddir/libsommelier.a.p/quirks/quirks.pb.h"
#include <google/protobuf/text_format.h>
#include "quirks/quirks.pb.h"
#include "quirks/sommelier-quirks.h"
#include <set>
#include "sommelier-window.h"  // NOLINT(build/include_directory)

void Quirks::Load(std::string textproto) {
  google::protobuf::TextFormat::MergeFromString(textproto, &active_config_);
  Update();
}

bool Quirks::IsEnabled(struct sl_window* window, int feature) {
  return enabled_features_.find(std::make_pair(
             window->steam_game_id, feature)) != enabled_features_.end();
}

void Quirks::Update() {
  enabled_features_.clear();

  for (quirks::SommelierRule rule : active_config_.sommelier()) {
    // For now, only support a single instance of a single
    // steam_game_id condition.
    if (rule.condition_size() != 1 ||
        !rule.condition()[0].has_steam_game_id()) {
      continue;
    }
    uint32_t id = rule.condition()[0].steam_game_id();

    for (int feature : rule.enable()) {
      enabled_features_.insert(std::make_pair(id, feature));
    }
    for (int feature : rule.disable()) {
      enabled_features_.erase(std::make_pair(id, feature));
    }
  }
}
