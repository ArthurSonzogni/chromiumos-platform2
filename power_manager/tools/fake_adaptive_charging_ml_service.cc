// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <cstdlib>
#include <memory>

#include <base/at_exit.h>
#include <base/check.h>
#include <base/logging.h>
#include <base/run_loop.h>
#include <base/task/single_thread_task_executor.h>
#include <brillo/daemons/dbus_daemon.h>
#include <brillo/dbus/dbus_method_response.h>
#include <brillo/flag_helper.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/bus.h>
#include <dbus/exported_object.h>
#include <dbus/message.h>
#include <dbus/object_proxy.h>

namespace {

constexpr char kDBusMethodName[] = "RequestAdaptiveChargingDecision";
// The size of the Adaptive Charging ml service response is fixed at 9 doubles.
constexpr size_t kResponseValuesSize = 9;

void RequestAdaptiveChargingDecision(
    const std::vector<double>& response_values,
    base::RepeatingClosure quit_closure,
    bool exit_after_prediction,
    dbus::MethodCall* method_call,
    dbus::ExportedObject::ResponseSender response_sender) {
  // We don't really care about the arguments passed in via `method_call`, since
  // we return constant values.
  auto response_ptr = std::make_unique<
      brillo::dbus_utils::DBusMethodResponse<bool, std::vector<double>>>(
      method_call, std::move(response_sender));
  response_ptr->Return(true, response_values);
  if (exit_after_prediction)
    quit_closure.Run();
}

};  // namespace

int main(int argc, char* argv[]) {
  // These mirror the fields from the PowerManagementPolicy protocol buffer.
  DEFINE_int32(prediction_hours, 0,
               "Default number of hours for the service to predict until "
               "unplug.");
  DEFINE_bool(exit_after_prediction, false,
              "Whether the service should exit after returning a prediction.");

  brillo::FlagHelper::Init(
      argc, argv,
      "Stops the existing Adaptive Charging ML service and creates a fake "
      "service that returns a prediction defined by the command line "
      "arguments.\n");
  base::AtExitManager at_exit_manager;
  base::SingleThreadTaskExecutor task_executor(base::MessagePumpType::IO);
  base::FileDescriptorWatcher watcher(task_executor.task_runner());
  base::RunLoop run_loop;
  std::vector<double> response_values =
      std::vector<double>(kResponseValuesSize, 0);

  CHECK(FLAGS_prediction_hours >= 0 &&
        FLAGS_prediction_hours < response_values.size())
      << "Argument --prediction_hours with value " << FLAGS_prediction_hours
      << " is outside of valid range [0, " << response_values.size() << "].";

  response_values[FLAGS_prediction_hours] = 1.0;

  // Stop the actual ml-service for Adaptive Charging. It's possible that it's
  // not running, so just report a warning in that case.
  const std::string cmd = "stop ml-service TASK=adaptive_charging";
  int ret = ::system(cmd.c_str());
  int exit_status = WEXITSTATUS(ret);
  if (ret == -1) {
    LOG(WARNING) << "fork() failed for calling `" << cmd << "`";
  } else if (exit_status) {
    LOG(WARNING) << "Command `" << cmd << "` failed with exit status "
                 << exit_status;
  }

  dbus::Bus::Options options;
  options.bus_type = dbus::Bus::SYSTEM;
  scoped_refptr<dbus::Bus> bus(new dbus::Bus(std::move(options)));
  CHECK(bus->Connect()) << "Failed to connect to system DBus";
  dbus::ExportedObject* exported_object = bus->GetExportedObject(
      dbus::ObjectPath(ml::kMachineLearningAdaptiveChargingServicePath));
  CHECK(exported_object) << "Failed to get exported object for "
                         << ml::kMachineLearningAdaptiveChargingServicePath
                         << ".";
  CHECK(exported_object->ExportMethodAndBlock(
      ml::kMachineLearningAdaptiveChargingInterfaceName, kDBusMethodName,
      base::BindRepeating(RequestAdaptiveChargingDecision, response_values,
                          run_loop.QuitClosure(), FLAGS_exit_after_prediction)))
      << "Failed to export method " << kDBusMethodName << " for interface "
      << ml::kMachineLearningAdaptiveChargingInterfaceName << ".";
  CHECK(bus->RequestOwnershipAndBlock(
      ml::kMachineLearningAdaptiveChargingServiceName,
      dbus::Bus::REQUIRE_PRIMARY))
      << "Failed to take ownership of DBus service "
      << ml::kMachineLearningAdaptiveChargingServiceName << ".";

  run_loop.Run();

  return 0;
}
