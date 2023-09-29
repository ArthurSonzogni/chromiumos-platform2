// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_SOMMELIER_QUIRKS_SOMMELIER_QUIRKS_H_
#define VM_TOOLS_SOMMELIER_QUIRKS_SOMMELIER_QUIRKS_H_

#include <set>
#include <string>
#include <utility>

#include <google/protobuf/message.h>
#include "quirks/quirks.pb.h"

class Quirks {
 public:
  // Parse `textproto` as a Config proto, and merge it into the active config.
  void Load(std::string textproto);

  // Call `LoadFromFile` for each filename separated by commas in `paths`.
  void LoadFromCommaSeparatedFiles(const char* paths);

  // Load a Config textproto from `path`, and merge it into the active config.
  void LoadFromFile(std::string path);

  // Whether the given Feature (from quirks.proto) is enabled for the given
  // `window`, according to the active config.
  bool IsEnabled(struct sl_window* window, int feature);

 private:
  // Repopulate `enabled_features_` from the rules in `active_config_`.
  void Update();

  // The active rules in protobuf form, accumulated from calls to `Load()`.
  quirks::Config active_config_;

  // The active config in a more easily queryable form.
  //
  // Each pair is built from a Steam Game ID and a Feature enum, indicating
  // that the Feature is enabled for windows with that STEAM_GAME property.
  std::set<std::pair<uint32_t, int>> enabled_features_;
};

#endif  // VM_TOOLS_SOMMELIER_QUIRKS_SOMMELIER_QUIRKS_H_
