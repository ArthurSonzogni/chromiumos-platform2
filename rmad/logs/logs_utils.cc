// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/logs/logs_utils.h"

#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <base/check.h>
#include <base/json/json_string_value_serializer.h>
#include <base/memory/scoped_refptr.h>
#include <base/strings/strcat.h>
#include <base/strings/stringprintf.h>
#include <base/strings/string_util.h>
#include <base/time/time.h>
#include <base/values.h>

#include "rmad/constants.h"
#include "rmad/logs/logs_constants.h"
#include "rmad/proto_bindings/rmad.pb.h"
#include "rmad/utils/json_store.h"
#include "rmad/utils/type_conversions.h"

namespace rmad {

namespace {

const char* GetStateName(RmadState::StateCase state) {
  auto it = kStateNames.find(state);
  CHECK(it != kStateNames.end());
  return it->second.data();
}

std::string JoinValueList(
    const base::Value::List* list,
    const std::function<std::string(const base::Value&)>& function,
    const std::string& separator) {
  auto begin = list->begin();
  auto end = list->end();
  std::ostringstream list_stream;
  if (begin != end) {
    list_stream << function(*begin);
    while (++begin != end) {
      list_stream << separator << function(*begin);
    }
  }
  return list_stream.str();
}

bool AddEventToJson(scoped_refptr<JsonStore> json_store,
                    RmadState::StateCase state,
                    LogEventType event_type,
                    base::Value&& details) {
  base::Value event(base::Value::Type::DICT);
  event.SetKey(kTimestamp, ConvertToValue(base::Time::Now().ToDoubleT()));
  event.SetKey(kStateId, ConvertToValue(static_cast<int>(state)));
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

std::string GetCalibrationStatusString(const base::Value::Dict& component) {
  const std::string component_name = *component.FindString(kLogComponent);
  const LogCalibrationStatus status = static_cast<LogCalibrationStatus>(
      component.FindInt(kLogCalibrationStatus).value());
  auto it = kLogCalibrationStatusMap.find(status);
  CHECK(it != kLogCalibrationStatusMap.end());
  return base::StrCat({component_name, " - ", it->second});
}

std::string GenerateTextLogString(scoped_refptr<JsonStore> json_store) {
  std::string generated_text_log;

  base::Value logs(base::Value::Type::DICT);
  json_store->GetValue(kLogs, &logs);
  const base::Value::List* events = logs.GetDict().FindList(kEvents);

  for (const base::Value& event : *events) {
    const base::Value::Dict& event_dict = event.GetDict();
    const int type = event_dict.FindInt(kType).value();
    const int current_state_id = event_dict.FindInt(kStateId).value();

    // Append the timestamp prefix.
    base::Time::Exploded exploded;
    base::Time::FromDoubleT(event_dict.FindDouble(kTimestamp).value())
        .LocalExplode(&exploded);
    generated_text_log.append(
        base::StringPrintf(kLogTimestampFormat, exploded.year, exploded.month,
                           exploded.day_of_month, exploded.hour,
                           exploded.minute, exploded.second));

    const base::Value::Dict* details = event_dict.FindDict(kDetails);
    switch (static_cast<LogEventType>(type)) {
      case LogEventType::kTransition: {
        const RmadState::StateCase from_state =
            static_cast<RmadState::StateCase>(
                details->FindInt(kFromStateId).value());
        const RmadState::StateCase to_state = static_cast<RmadState::StateCase>(
            details->FindInt(kToStateId).value());
        generated_text_log.append(base::StringPrintf(kLogTransitionFormat,
                                                     GetStateName(from_state),
                                                     GetStateName(to_state)));
        break;
      }
      case LogEventType::kError: {
        const RmadErrorCode error_code = static_cast<RmadErrorCode>(
            details->FindInt(kOccurredError).value());
        generated_text_log.append(base::StringPrintf(
            kLogErrorFormat,
            GetStateName(static_cast<RmadState::StateCase>(current_state_id)),
            RmadErrorCode_Name(error_code).c_str()));
        break;
      }
      case LogEventType::kData: {
        const RmadState::StateCase current_state =
            static_cast<RmadState::StateCase>(current_state_id);
        generated_text_log.append(base::StringPrintf(
            kLogDetailPrefixFormat, GetStateName(current_state)));

        switch (current_state) {
          case RmadState::kComponentsRepair: {
            const bool is_mlb_repair =
                details->FindBool(kLogReworkSelected).value();
            if (is_mlb_repair) {
              generated_text_log.append(kLogSelectComponentsReworkString);
            } else {
              const base::Value::List* components =
                  details->FindList(kLogReplacedComponents);
              const std::string component_list = JoinValueList(
                  components,
                  [](const base::Value& value) { return value.GetString(); },
                  ", ");
              generated_text_log.append(base::StringPrintf(
                  kLogSelectComponentsFormat, component_list.c_str()));
            }
            break;
          }
          case RmadState::kDeviceDestination: {
            generated_text_log.append(base::StringPrintf(
                kLogChooseDeviceDestinationFormat,
                (*details->FindString(kLogDestination)).c_str()));
            break;
          }
          case RmadState::kWipeSelection: {
            const std::string wipe_device =
                details->FindBool(kLogWipeDevice).value() ? "wipe" : "keep";
            generated_text_log.append(base::StringPrintf(
                kLogWipeSelectionFormat, wipe_device.c_str()));
            break;
          }
          case RmadState::kWpDisableMethod: {
            generated_text_log.append(base::StringPrintf(
                kLogWpDisableFormat,
                (*details->FindString(kLogWpDisableMethod)).c_str()));
            break;
          }
          case RmadState::kWpDisableRsu: {
            generated_text_log.append(base::StringPrintf(
                kLogRsuChallengeFormat,
                (*details->FindString(kLogRsuChallengeCode)).c_str()));
            break;
          }
          case RmadState::kRestock: {
            generated_text_log.append(
                details->FindBool(kLogRestockOption).value()
                    ? kLogRestockShutdownString
                    : kLogRestockContinueString);
            break;
          }
          case RmadState::kCheckCalibration: {
            const base::Value::List* components =
                details->FindList(kLogCalibrationComponents);
            const std::string component_list = JoinValueList(
                components,
                [](const base::Value& value) {
                  return GetCalibrationStatusString(value.GetDict());
                },
                ", ");
            generated_text_log.append(base::StringPrintf(
                kLogCalibrationFormat, component_list.c_str()));
            break;
          }
          case RmadState::kUpdateRoFirmware: {
            const FirmwareUpdateStatus status =
                static_cast<FirmwareUpdateStatus>(
                    details->FindInt(kFirmwareStatus).value());
            auto it = kFirmwareUpdateStatusMap.find(status);
            CHECK(it != kFirmwareUpdateStatusMap.end());
            generated_text_log.append(it->second.data());
            break;
          }
          default:
            break;
        }
      }
    }
    generated_text_log.append("\n");
  }

  return generated_text_log;
}

}  // namespace

std::string GenerateCompleteLogsString(scoped_refptr<JsonStore> json_store) {
  return GenerateTextLogString(json_store);
}

bool RecordStateTransitionToLogs(scoped_refptr<JsonStore> json_store,
                                 RmadState::StateCase from_state,
                                 RmadState::StateCase to_state) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kFromStateId, ConvertToValue(static_cast<int>(from_state)));
  details.SetKey(kToStateId, ConvertToValue(static_cast<int>(to_state)));

  return AddEventToJson(json_store, from_state, LogEventType::kTransition,
                        std::move(details));
}

bool RecordOccurredErrorToLogs(scoped_refptr<JsonStore> json_store,
                               RmadState::StateCase current_state,
                               RmadErrorCode error) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kOccurredError, ConvertToValue(static_cast<int>(error)));

  return AddEventToJson(json_store, current_state, LogEventType::kError,
                        std::move(details));
}

bool RecordSelectedComponentsToLogs(
    scoped_refptr<JsonStore> json_store,
    const std::vector<std::string>& replaced_components,
    bool is_mlb_repair) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogReplacedComponents, ConvertToValue(replaced_components));
  details.SetKey(kLogReworkSelected, ConvertToValue(is_mlb_repair));

  return AddEventToJson(json_store, RmadState::kComponentsRepair,
                        LogEventType::kData, std::move(details));
}

bool RecordDeviceDestinationToLogs(scoped_refptr<JsonStore> json_store,
                                   const std::string& device_destination) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogDestination, ConvertToValue(device_destination));

  return AddEventToJson(json_store, RmadState::kDeviceDestination,
                        LogEventType::kData, std::move(details));
}

bool RecordWipeDeviceToLogs(scoped_refptr<JsonStore> json_store,
                            bool wipe_device) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogWipeDevice, ConvertToValue(wipe_device));

  return AddEventToJson(json_store, RmadState::kWipeSelection,
                        LogEventType::kData, std::move(details));
}

bool RecordWpDisableMethodToLogs(scoped_refptr<JsonStore> json_store,
                                 const std::string& wp_disable_method) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogWpDisableMethod, ConvertToValue(wp_disable_method));

  return AddEventToJson(json_store, RmadState::kWpDisableMethod,
                        LogEventType::kData, std::move(details));
}

bool RecordRsuChallengeCodeToLogs(scoped_refptr<JsonStore> json_store,
                                  const std::string& challenge_code,
                                  const std::string& hwid) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogRsuChallengeCode, ConvertToValue(challenge_code));
  details.SetKey(kLogRsuHwid, ConvertToValue(hwid));

  return AddEventToJson(json_store, RmadState::kWpDisableRsu,
                        LogEventType::kData, std::move(details));
}

bool RecordRestockOptionToLogs(scoped_refptr<JsonStore> json_store,
                               bool restock) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogRestockOption, ConvertToValue(restock));

  return AddEventToJson(json_store, RmadState::kRestock, LogEventType::kData,
                        std::move(details));
}

bool RecordComponentCalibrationStatusToLogs(
    scoped_refptr<JsonStore> json_store,
    const std::vector<std::pair<std::string, LogCalibrationStatus>>&
        component_statuses) {
  base::Value components(base::Value::Type::LIST);
  for (auto& component_status : component_statuses) {
    base::Value component(base::Value::Type::DICT);
    component.SetKey(kLogComponent, ConvertToValue(component_status.first));
    component.SetKey(kLogCalibrationStatus,
                     ConvertToValue(static_cast<int>(component_status.second)));
    components.Append(std::move(component));
  }

  base::Value details(base::Value::Type::DICT);
  details.SetKey(kLogCalibrationComponents, std::move(components));

  return AddEventToJson(json_store, RmadState::kCheckCalibration,
                        LogEventType::kData, std::move(details));
}

bool RecordFirmwareUpdateStatusToLogs(scoped_refptr<JsonStore> json_store,
                                      FirmwareUpdateStatus status) {
  base::Value details(base::Value::Type::DICT);
  details.SetKey(kFirmwareStatus, ConvertToValue(static_cast<int>(status)));

  return AddEventToJson(json_store, RmadState::kUpdateRoFirmware,
                        LogEventType::kData, std::move(details));
}

}  // namespace rmad
