// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_FAKE_PROBE_CONFIG_LOADER_H_
#define RUNTIME_PROBE_FAKE_PROBE_CONFIG_LOADER_H_

#include <optional>
#include <utility>

#include <base/files/file_path.h>

#include "runtime_probe/probe_config_loader.h"

namespace runtime_probe {

class FakeProbeConfigLoader : public ProbeConfigLoader {
 public:
  std::optional<ProbeConfigData> LoadDefault() const override {
    if (!config_)
      return std::nullopt;
    return ProbeConfigData{.path = config_->path,
                           .config = config_->config.Clone(),
                           .sha1_hash = config_->sha1_hash};
  }

  std::optional<ProbeConfigData> LoadFromFile(
      const base::FilePath& file_path) const override {
    if (!config_)
      return std::nullopt;
    return ProbeConfigData{.path = file_path,
                           .config = config_->config.Clone(),
                           .sha1_hash = config_->sha1_hash};
  }

  void set_fake_probe_config_data(ProbeConfigData config) {
    config_ = std::move(config);
  }

  void clear_fake_probe_config_data() { config_ = std::nullopt; }

 private:
  std::optional<ProbeConfigData> config_;
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_FAKE_PROBE_CONFIG_LOADER_H_
