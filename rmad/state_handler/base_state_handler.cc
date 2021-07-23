// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/state_handler/base_state_handler.h"

#include <map>
#include <string>

#include <base/base64.h>
#include <base/strings/string_number_conversions.h>

#include "rmad/constants.h"

namespace rmad {

BaseStateHandler::BaseStateHandler(scoped_refptr<JsonStore> json_store)
    : json_store_(json_store) {}

const RmadState& BaseStateHandler::GetState() const {
  return state_;
}

bool BaseStateHandler::StoreState() {
  std::map<std::string, std::string> state_map;
  json_store_->GetValue(kStateMap, &state_map);

  std::string key = base::NumberToString(GetStateCase());
  std::string serialized_string, serialized_string_base64;
  state_.SerializeToString(&serialized_string);
  base::Base64Encode(serialized_string, &serialized_string_base64);

  state_map[key] = serialized_string_base64;
  return json_store_->SetValue(kStateMap, state_map);
}

bool BaseStateHandler::RetrieveState() {
  if (std::map<std::string, std::string> state_map;
      json_store_->GetValue(kStateMap, &state_map)) {
    std::string key = base::NumberToString(GetStateCase());
    auto it = state_map.find(key);
    if (it != state_map.end()) {
      std::string serialized_string;
      DCHECK(base::Base64Decode(it->second, &serialized_string));
      return state_.ParseFromString(serialized_string);
    }
  }
  return false;
}

}  // namespace rmad
