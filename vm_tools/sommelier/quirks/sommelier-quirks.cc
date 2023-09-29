// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "builddir/libsommelier.a.p/quirks/quirks.pb.h"
#include <fcntl.h>
#include <google/protobuf/io/zero_copy_stream_impl.h>
#include <google/protobuf/text_format.h>
#include "quirks/quirks.pb.h"
#include "quirks/sommelier-quirks.h"
#include <set>
#include "sommelier-window.h"  // NOLINT(build/include_directory)
#include <string.h>
#include <unistd.h>

void Quirks::Load(std::string textproto) {
  google::protobuf::TextFormat::MergeFromString(textproto, &active_config_);
  Update();
}

void Quirks::LoadFromCommaSeparatedFiles(const char* paths) {
  const char* start = paths;
  const char* end;
  do {
    // Find the next comma (or end of string).
    end = strchrnul(start, ',');
    // `start` and `end` delimit a path; load rules from it.
    std::string path(start, end - start);
    LoadFromFile(path);
    // The next string starts after the comma
    start = end + 1;
    // Terminate on null char.
  } while (*end != '\0');
}

void Quirks::LoadFromFile(std::string path) {
  int fd = open(path.c_str(), O_RDONLY);
  if (fd < 0) {
    const char* e = strerror(errno);
    fprintf(stderr, "Failed to open quirks config: %s: %s\n", path.c_str(), e);
    return;
  }
  google::protobuf::io::FileInputStream f(fd);
  if (google::protobuf::TextFormat::Merge(&f, &active_config_)) {
    Update();
  }
  close(fd);
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
