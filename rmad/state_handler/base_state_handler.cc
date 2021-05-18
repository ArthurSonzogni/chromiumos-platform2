// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <string>

#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

namespace rmad {

BaseStateHandler::BaseStateHandler(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

bool BaseStateHandler::StoreState() {
  std::map<std::string, std::string> state_map;
  json_store_->GetValue(kStateMap, &state_map);

  std::string key = base::NumberToString(GetStateCase());
  std::string serialized_string;
  state_.SerializeToString(&serialized_string);

  state_map.insert({key, serialized_string});
  return json_store_->SetValue(kStateMap, state_map);
}

bool BaseStateHandler::RetrieveState() {
  if (std::map<std::string, std::string> state_map;
      json_store_->GetValue(kStateMap, &state_map)) {
    std::string key = base::NumberToString(GetStateCase());
    auto it = state_map.find(key);
    if (it != state_map.end()) {
      return state_.ParseFromString(it->second);
    }
  }
  return false;
}

}  // namespace rmad
