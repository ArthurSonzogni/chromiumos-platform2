// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef RUNTIME_PROBE_PROBE_CONFIG_H_
#define RUNTIME_PROBE_PROBE_CONFIG_H_

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <base/files/file_path.h>
#include <base/values.h>
#include <gtest/gtest.h>

#include "runtime_probe/component_category.h"

namespace runtime_probe {

class ProbeConfig {
  // Holds a probe config.
  //
  // The input will be in JSON format with following schema::
  //   {
  //     <category:string>: {
  //       <component_name:string>: <statement:ProbeStatement>,
  //       ...
  //     }
  //   }

 public:
  // Factory method that creates the probe config from the given file path.
  // Return |std::nullopt| if loading fails.
  static std::optional<ProbeConfig> FromFile(const base::FilePath& file_path);

  // Factory method that creates the probe config from the given dictionary.
  // Return |std::nullopt| if loading fails.
  static std::optional<ProbeConfig> FromValue(const base::Value& dv);

  // Evaluates the probe config.
  //
  // @param category: specifies the components to probe.
  // @return base::Value with the following format:
  //   {
  //     <category:string>: [
  //       {
  //         "name": <component_name:string>,
  //         "values": <probed_values of ProbeStatement>,
  //         "information": <information of ProbeStatement>
  //       }
  //     ]
  //   }
  base::Value Eval(const std::vector<std::string>& category) const;

  // Evaluates the probe config.
  //
  // This is the same as calling `this->eval({keys of category_})`.
  base::Value Eval() const;

  // Gets the component category with given name or return nullptr on failure.
  ComponentCategory* GetComponentCategory(
      const std::string& category_name) const;

  // Checksum of the probe config text in SHA1 hash.
  const std::string& checksum() const { return checksum_; }

  // Path to the probe config file.
  const base::FilePath& path() const { return path_; }

 private:
  // Private constructor used by factory methods only.
  ProbeConfig() = default;

  std::map<std::string, std::unique_ptr<ComponentCategory>> category_;

  std::string checksum_;

  base::FilePath path_;

  FRIEND_TEST(ProbeConfigTest, LoadConfig);
};

}  // namespace runtime_probe

#endif  // RUNTIME_PROBE_PROBE_CONFIG_H_
