// Copyright 2020 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event.h"

#include <cstdlib>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <base/at_exit.h>
#include <base/bind.h>
#include <base/logging.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>
#include <brillo/message_loops/base_message_loop.h>
#include <brillo/syslog_logging.h>

#include "diagnostics/cros_health_tool/event/event_subscriber.h"

namespace diagnostics {

namespace {

namespace mojo_ipc = ::chromeos::cros_healthd::mojom;

enum class EventCategory {
  kLid,
  kPower,
  kBluetooth,
  kNetwork,
  kAudio,
};

constexpr std::pair<const char*, EventCategory> kCategorySwitches[] = {
    {"lid", EventCategory::kLid},
    {"power", EventCategory::kPower},
    {"bluetooth", EventCategory::kBluetooth},
    {"network", EventCategory::kNetwork},
    {"audio", EventCategory::kAudio},
};

// Create a stringified list of the category names for use in help.
std::string GetCategoryHelp() {
  std::stringstream ss;
  ss << "Category of events to subscribe to: [";
  const char* sep = "";
  for (auto pair : kCategorySwitches) {
    ss << sep << pair.first;
    sep = ", ";
  }
  ss << "]";
  return ss.str();
}

}  // namespace

int event_main(int argc, char** argv) {
  std::string category_help = GetCategoryHelp();
  DEFINE_string(category, "", category_help.c_str());
  DEFINE_uint32(length_seconds, 10, "Number of seconds to listen for events.");
  brillo::FlagHelper::Init(argc, argv,
                           "event - Device event subscription tool.");
  brillo::InitLog(brillo::kLogToSyslog | brillo::kLogToStderrIfTty);

  base::AtExitManager at_exit_manager;

  std::map<std::string, EventCategory> switch_to_category(
      std::begin(kCategorySwitches), std::end(kCategorySwitches));

  logging::InitLogging(logging::LoggingSettings());

  brillo::BaseMessageLoop message_loop;

  // Make sure at least one category is specified.
  if (FLAGS_category == "") {
    LOG(ERROR) << "No category specified.";
    return EXIT_FAILURE;
  }
  // Validate the category flag.
  auto iterator = switch_to_category.find(FLAGS_category);
  if (iterator == switch_to_category.end()) {
    LOG(ERROR) << "Invalid category: " << FLAGS_category;
    return EXIT_FAILURE;
  }

  // Subscribe to the specified category.
  EventSubscriber event_subscriber;
  bool success = false;
  switch (iterator->second) {
    case EventCategory::kLid:
      success = event_subscriber.SubscribeToLidEvents();
      break;
    case EventCategory::kPower:
      success = event_subscriber.SubscribeToPowerEvents();
      break;
    case EventCategory::kBluetooth:
      success = event_subscriber.SubscribeToBluetoothEvents();
      break;
    case EventCategory::kNetwork:
      success = event_subscriber.SubscribeToNetworkEvents();
      break;
    case EventCategory::kAudio:
      success = event_subscriber.SubscribeToAudioEvents();
      break;
  }

  if (!success) {
    LOG(ERROR) << "Unable to subscribe to events";
    return EXIT_FAILURE;
  }

  // Schedule an exit after |FLAGS_length_seconds|.
  message_loop.PostDelayedTask(
      FROM_HERE,
      base::BindOnce([](brillo::BaseMessageLoop* loop) { loop->BreakLoop(); },
                     &message_loop),
      base::TimeDelta::FromSeconds(FLAGS_length_seconds));

  message_loop.Run();

  return EXIT_SUCCESS;
}

}  // namespace diagnostics
