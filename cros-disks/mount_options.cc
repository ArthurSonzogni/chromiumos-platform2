// Copyright 2011 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cros-disks/mount_options.h"

#include <algorithm>

#include <base/containers/adapters.h>
#include <base/containers/contains.h>
#include <base/strings/string_util.h>
#include <base/strings/strcat.h>

namespace cros_disks {

namespace {
bool FindLastElementStartingWith(const std::vector<std::string>& container,
                                 std::string_view prefix,
                                 std::string* result) {
  for (const auto& element : base::Reversed(container)) {
    if (base::StartsWith(element, prefix, base::CompareCase::SENSITIVE)) {
      *result = element;
      return true;
    }
  }
  return false;
}
}  // namespace

bool IsReadOnlyMount(const std::vector<std::string>& options) {
  for (const auto& option : base::Reversed(options)) {
    if (option == "ro")
      return true;
    if (option == "rw")
      return false;
  }
  return false;
}

bool GetParamValue(const std::vector<std::string>& params,
                   std::string_view name,
                   std::string* value) {
  if (!FindLastElementStartingWith(params, base::StrCat({name, "="}), value)) {
    return false;
  }
  *value = value->substr(name.length() + 1);
  return true;
}

void SetParamValue(std::vector<std::string>* params,
                   std::string_view name,
                   std::string_view value) {
  params->emplace_back(base::StrCat({name, "=", value}));
}

bool HasExactParam(const std::vector<std::string>& params,
                   std::string_view param) {
  return base::Contains(params, param);
}

size_t RemoveParamsEqualTo(std::vector<std::string>* params,
                           std::string_view param) {
  return std::erase(*params, param);
}

size_t RemoveParamsWithSameName(std::vector<std::string>* params,
                                std::string_view name) {
  std::string prefix = base::StrCat({name, "="});
  return std::erase_if(*params, [prefix](const std::string& value) {
    return base::StartsWith(value, prefix, base::CompareCase::SENSITIVE);
  });
}

bool JoinParamsIntoOptions(const std::vector<std::string>& params,
                           std::string* out) {
  for (const auto& element : params) {
    if (element.find(',') != std::string::npos)
      return false;
  }
  *out = base::JoinString(params, ",");
  return true;
}

}  // namespace cros_disks
