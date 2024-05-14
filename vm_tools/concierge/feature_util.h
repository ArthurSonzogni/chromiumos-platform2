// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_FEATURE_UTIL_H_
#define VM_TOOLS_CONCIERGE_FEATURE_UTIL_H_

#include <map>
#include <optional>
#include <string>

namespace vm_tools::concierge {

// Given a map of parameter names and corresponding values, |params|, attempts
// to find the value for |key| and parse it as an integer. Returns the parsed
// value if |key| was found, or nullopt if some error occurred.
std::optional<int> FindIntValue(
    const std::map<std::string, std::string>& params, std::string key);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_FEATURE_UTIL_H_
