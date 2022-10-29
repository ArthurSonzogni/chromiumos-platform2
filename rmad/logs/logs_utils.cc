// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/logs/logs_utils.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

#include <base/json/json_string_value_serializer.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <base/values.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/type_conversions.h"

namespace rmad {

namespace {

bool AddEventToJson(scoped_refptr<JsonStore> json_store,
                    LogEventType event_type,
                    base::Value&& details) {
  base::Value event(base::Value::Type::DICT);
  event.SetKey(kTimestamp, ConvertToValue(base::Time::Now().ToDoubleT()));
  event.SetKey(kType, ConvertToValue(static_cast<int>(event_type)));
  event.SetKey(kDetails, std::move(details));

  base::Value logs(base::Value::Type::DICT);
  if (json_store->GetValue(kLogs, &logs)) {
    CHECK(logs.is_dict());
  }

  // EnsureList() returns a pointer to the `events` JSON so no need to add it
  // back to `logs`.
  base::Value::List* events = logs.GetDict().EnsureList(kEvents);
  events->Append(std::move(event));

  return json_store->SetValue(kLogs, std::move(logs));
}

}  // namespace

bool RecordStateTransitionToLogs(scoped_refptr<JsonStore> json_store,
                                 RmadState::StateCase from_state,
                                 RmadState::StateCase to_state) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kFromStateId, ConvertToValue(static_cast<int>(from_state)));
  details.SetKey(kToStateId, ConvertToValue(static_cast<int>(to_state)));

  return AddEventToJson(json_store, LogEventType::kTransition,
                        std::move(details));
}

bool RecordSelectedComponentsToLogs(
    scoped_refptr<JsonStore> json_store,
    const std::vector<std::string>& replaced_components,
    bool is_mlb_repair) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogReplacedComponents, ConvertToValue(replaced_components));
  details.SetKey(kLogReworkSelected, ConvertToValue(is_mlb_repair));

  return AddEventToJson(json_store, LogEventType::kData, std::move(details));
}

bool RecordDeviceDestinationToLogs(scoped_refptr<JsonStore> json_store,
                                   const std::string& device_destination) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogDestination, ConvertToValue(device_destination));

  return AddEventToJson(json_store, LogEventType::kData, std::move(details));
}

bool RecordWipeDeviceToLogs(scoped_refptr<JsonStore> json_store,
                            bool wipe_device) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogWipeDevice, ConvertToValue(wipe_device));

  return AddEventToJson(json_store, LogEventType::kData, std::move(details));
}

bool RecordWpDisableMethodToLogs(scoped_refptr<JsonStore> json_store,
                                 const std::string& wp_disable_method) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogWpDisableMethod, ConvertToValue(wp_disable_method));

  return AddEventToJson(json_store, LogEventType::kData, std::move(details));
}

bool RecordRsuChallengeCodeToLogs(scoped_refptr<JsonStore> json_store,
                                  const std::string& challenge_code,
                                  const std::string& hwid) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogRsuChallengeCode, ConvertToValue(challenge_code));
  details.SetKey(kLogRsuHwid, ConvertToValue(hwid));

  return AddEventToJson(json_store, LogEventType::kData, std::move(details));
}

}  // namespace rmad
