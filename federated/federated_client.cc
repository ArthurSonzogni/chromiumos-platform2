// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "federated/federated_client.h"

#include <string>
#include <utility>

#include <base/logging.h>
#include <base/notreached.h>

#include "federated/device_status_monitor.h"
#include "federated/example_database.h"
#include "federated/federated_metadata.h"
#include "federated/protos/cros_events.pb.h"
#include "federated/protos/cros_example_selector_criteria.pb.h"
#include "federated/utils.h"

namespace federated {

namespace {
#if USE_LOCAL_FEDERATED_SERVER
constexpr base::TimeDelta kDefaultRetryWindow = base::Seconds(30);
constexpr base::TimeDelta kMinimalRetryWindow = base::Seconds(10);
#else
// TODO(b/239623649): discussion required about the default window.
constexpr base::TimeDelta kDefaultRetryWindow = base::Seconds(60 * 30);

// To avoid spam, retry window should not be shorter than kMinimalRetryWindow.
constexpr base::TimeDelta kMinimalRetryWindow = base::Seconds(60);
#endif

// TODO(b/251378482): Just dummpy impl for now, might need to log to UMA.
void LogCrosEvent(const fcp::client::CrosEvent& cros_event) {
  LOG(INFO) << "In LogCrosEvent, model_id is " << cros_event.model_id();
  DVLOG(1) << "cros_event is " << cros_event.DebugString();

  if (cros_event.has_eligibility_eval_checkin()) {
    LOG(INFO) << "cros_event has_eligibility_eval_checkin";
  } else if (cros_event.has_eligibility_eval_plan_received()) {
    LOG(INFO) << "cros_event has_eligibility_eval_plan_received";
  } else if (cros_event.has_eligibility_eval_not_configured()) {
    LOG(INFO) << "cros_event.has_eligibility_eval_not_configured";
  } else if (cros_event.has_eligibility_eval_rejected()) {
    LOG(INFO) << "cros_event.has_eligibility_eval_rejected";
  } else if (cros_event.has_checkin()) {
    LOG(INFO) << "cros_event.has_checkin";
  } else if (cros_event.has_checkin_finished()) {
    LOG(INFO) << "cros_event.has_checkin_finished";
  } else if (cros_event.has_rejected()) {
    LOG(INFO) << "cros_event.has_rejected";
  } else if (cros_event.has_report_started()) {
    LOG(INFO) << "cros_event.has_report_started";
  } else if (cros_event.has_report_finished()) {
    LOG(INFO) << "cros_event.has_report_finished";
  } else if (cros_event.has_plan_execution_started()) {
    LOG(INFO) << "cros_event.has_plan_execution_started";
  } else if (cros_event.has_epoch_started()) {
    LOG(INFO) << "cros_event.has_epoch_started";
  } else if (cros_event.has_tensorflow_error()) {
    LOG(ERROR) << "cros_event.has_tensorflow_error";
  } else if (cros_event.has_io_error()) {
    LOG(ERROR) << "cros_event.has_io_error";
  } else if (cros_event.has_example_selector_error()) {
    LOG(ERROR) << "cros_event.has_example_selector_error";
  } else if (cros_event.has_interruption()) {
    LOG(INFO) << "cros_event.has_interruption";
  } else if (cros_event.has_epoch_completed()) {
    LOG(INFO) << "cros_event.has_epoch_completed";
  } else if (cros_event.has_stats()) {
    LOG(INFO) << "cros_event.has_stats";
  } else if (cros_event.has_plan_completed()) {
    LOG(INFO) << "cros_event.has_plan_completed";
  } else {
    LOG(INFO) << "cros_event doesn't have any event log";
  }
}

// TODO(b/251378482): Just dummpy impl for now, might need to log to UMA.
void LogCrosSecAggEvent(const fcp::client::CrosSecAggEvent& cros_secagg_event) {
  LOG(INFO) << "In LogCrosSecAggEvent, session_id is "
            << cros_secagg_event.execution_session_id();

  if (cros_secagg_event.has_state_transition())
    LOG(INFO) << "cros_secagg_event.has_state_transition";
  else if (cros_secagg_event.has_error())
    LOG(ERROR) << "cros_secagg_event.has_error";
  else if (cros_secagg_event.has_abort())
    LOG(INFO) << "cros_secagg_event.has_abort";
  else
    LOG(INFO) << "cros_secagg_event doesn't have any event log";
}

}  // namespace

FederatedClient::Context::Context(
    const std::string& client_name,
    const DeviceStatusMonitor* const device_status_monitor,
    const StorageManager* const storage_manager)
    : client_name_(client_name),
      device_status_monitor_(device_status_monitor),
      storage_manager_(storage_manager) {}

FederatedClient::Context::~Context() = default;

bool FederatedClient::Context::PrepareExamples(const char* const criteria_data,
                                               const int criteria_data_size,
                                               void* const context) {
  fcp::client::CrosExampleSelectorCriteria criteria;
  if (!criteria.ParseFromArray(criteria_data, criteria_data_size)) {
    LOG(ERROR) << "Failed to parse criteria.";
    return false;
  }

  auto* typed_context = static_cast<FederatedClient::Context*>(context);

  std::optional<ExampleDatabase::Iterator> example_iterator =
      typed_context->storage_manager_->GetExampleIterator(
          typed_context->client_name_, criteria);
  if (!example_iterator.has_value()) {
    DVLOG(1) << "Client " << typed_context->client_name_
             << " failed to prepare examples.";
    return false;
  }

  typed_context->example_iterator_ = std::move(example_iterator.value());
  return true;
}

bool FederatedClient::Context::GetNextExample(const char** const data,
                                              int* const size,
                                              bool* const end,
                                              void* const context) {
  if (context == nullptr)
    return false;

  const absl::StatusOr<ExampleRecord> record =
      static_cast<FederatedClient::Context*>(context)->example_iterator_.Next();

  if (absl::IsInvalidArgument(record.status())) {
    return false;
  }

  if (record.ok()) {
    *end = false;
    *size = record->serialized_example.size();
    char* const str_data = new char[*size];
    record->serialized_example.copy(str_data, *size);
    *data = str_data;
  } else {
    DCHECK(absl::IsOutOfRange(record.status()));
    *end = true;
  }

  return true;
}

void FederatedClient::Context::FreeExample(const char* const data,
                                           void* const context) {
  delete[] data;
}

bool FederatedClient::Context::TrainingConditionsSatisfied(
    void* const context) {
  if (context == nullptr)
    return false;

  return static_cast<FederatedClient::Context*>(context)
      ->device_status_monitor_->TrainingConditionsSatisfied();
}

void FederatedClient::Context::PublishEvent(const char* const event,
                                            const int size,
                                            void* const context) {
  if (context == nullptr) {
    LOG(ERROR) << "PublishEvent gets nullptr context.";
    return;
  }

  fcp::client::CrosEventLog event_log;
  if (!event_log.ParseFromArray(event, size)) {
    LOG(ERROR) << "Failed to parse event_log.";
    return;
  }

  if (event_log.has_event()) {
    LogCrosEvent(event_log.event());
  } else if (event_log.has_secagg_event()) {
    LogCrosSecAggEvent(event_log.secagg_event());
  } else {
    LOG(ERROR) << "event_log has no content";
  }
}

FederatedClient::FederatedClient(
    const FlRunPlanFn run_plan,
    const FlFreeRunPlanResultFn free_run_plan_result,
    const std::string& service_uri,
    const std::string& api_key,
    const ClientConfigMetadata client_config,
    const DeviceStatusMonitor* const device_status_monitor)
    : run_plan_(run_plan),
      free_run_plan_result_(free_run_plan_result),
      service_uri_(service_uri),
      api_key_(api_key),
      client_config_(client_config),
      next_retry_delay_(kDefaultRetryWindow),
      device_status_monitor_(device_status_monitor) {}

FederatedClient::~FederatedClient() = default;

void FederatedClient::RunPlan(const StorageManager* const storage_manager) {
  DCHECK(!storage_manager->sanitized_username().empty())
      << "storage_manager->sanitized_username() is unexpectedly empty!";

  FederatedClient::Context context(client_config_.name, device_status_monitor_,
                                   storage_manager);

  const std::string base_dir_in_cryptohome =
      GetBaseDir(storage_manager->sanitized_username(), client_config_.name)
          .value();
  const FlTaskEnvironment env = {
      &FederatedClient::Context::PrepareExamples,
      &FederatedClient::Context::GetNextExample,
      &FederatedClient::Context::FreeExample,
      &FederatedClient::Context::TrainingConditionsSatisfied,
      &FederatedClient::Context::PublishEvent,
      base_dir_in_cryptohome.c_str(),
      &context};

  FlRunPlanResult result =
      (*run_plan_)(env, service_uri_.c_str(), api_key_.c_str(),
                   /*population_name=*/client_config_.name.c_str(),
                   client_config_.retry_token.c_str());

  // TODO(b/251378482): maybe log the event to UMA
  if (result.status == CONTRIBUTED || result.status == REJECTED_BY_SERVER) {
    DVLOG(1) << "result.status = " << result.status;
    DVLOG(1) << "result.retry_token = " << result.retry_token;
    DVLOG(1) << "result.delay_usecs = " << result.delay_usecs;
    client_config_.retry_token = std::string(result.retry_token);
    next_retry_delay_ = base::Microseconds(result.delay_usecs);

    // TODO(b/239623649): result.delay_usecs may be 0 when setup is wrong, now I
    // set next_retry_delay_ to kMinimalRetryWindow to avoid spam, consider
    // stopping retry in this case because it's very likely to fail again.
    if (next_retry_delay_ < kMinimalRetryWindow)
      next_retry_delay_ = kMinimalRetryWindow;

  } else {
    DVLOG(1) << "Failed to checkin with the servce, result.status = "
             << result.status;
    next_retry_delay_ = kDefaultRetryWindow;
  }

  (*free_run_plan_result_)(result);
}

void FederatedClient::ResetRetryDelay() {
  next_retry_delay_ = kDefaultRetryWindow;
}

std::string FederatedClient::GetClientName() const {
  return client_config_.name;
}

}  // namespace federated
