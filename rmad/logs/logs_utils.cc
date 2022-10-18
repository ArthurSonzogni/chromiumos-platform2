// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/logs/logs_utils.h"

#include <map>
#include <utility>

#include <base/json/json_string_value_serializer.h>
#include <base/memory/scoped_refptr.h>
#include <base/time/time.h>
#include <base/values.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"

namespace rmad {

namespace {

bool AddEventToJson(scoped_refptr<JsonStore> json_store,
                    double timestamp,
                    LogEventType event_type,
                    base::Value&& details) {
  base::Value event(base::Value::Type::DICT);
  event.SetKey(kTimestamp, base::Value(timestamp));
  event.SetKey(kType, base::Value(static_cast<int>(event_type)));
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
  details.SetKey(kFromStateId, base::Value(static_cast<int>(from_state)));
  details.SetKey(kToStateId, base::Value(static_cast<int>(to_state)));

  return AddEventToJson(json_store, /*timestamp=*/base::Time::Now().ToDoubleT(),
                        LogEventType::kTransition, std::move(details));
}

}  // namespace rmad
