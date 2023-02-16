// Copyright 2020 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/event/event.h"

#include <cstdlib>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <base/logging.h>
#include <base/notreached.h>
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/cros_health_tool/event/event_subscriber.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

constexpr std::pair<const char*, mojom::EventCategoryEnum> kCategorySwitches[] =
    {
        {"lid", mojom::EventCategoryEnum::kLid},
        {"power", mojom::EventCategoryEnum::kPower},
        {"bluetooth", mojom::EventCategoryEnum::kBluetooth},
        {"network", mojom::EventCategoryEnum::kNetwork},
        {"audio", mojom::EventCategoryEnum::kAudio},
        {"thunderbolt", mojom::EventCategoryEnum::kThunderbolt},
        {"usb", mojom::EventCategoryEnum::kUsb},
        {"audio_jack", mojom::EventCategoryEnum::kAudioJack},
        {"sd_card", mojom::EventCategoryEnum::kSdCard},
        {"keyboard_diagnostic", mojom::EventCategoryEnum::kKeyboardDiagnostic},
        {"touchpad", mojom::EventCategoryEnum::kTouchpad},
        {"hdmi", mojom::EventCategoryEnum::kHdmi},
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

  std::map<std::string, mojom::EventCategoryEnum> switch_to_category(
      std::begin(kCategorySwitches), std::end(kCategorySwitches));

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
  base::RunLoop run_loop;
  EventSubscriber event_subscriber;
  switch (iterator->second) {
    case mojom::EventCategoryEnum::kAudio:
    case mojom::EventCategoryEnum::kAudioJack:
    case mojom::EventCategoryEnum::kBluetooth:
    case mojom::EventCategoryEnum::kKeyboardDiagnostic:
    case mojom::EventCategoryEnum::kLid:
    case mojom::EventCategoryEnum::kPower:
    case mojom::EventCategoryEnum::kSdCard:
    case mojom::EventCategoryEnum::kThunderbolt:
    case mojom::EventCategoryEnum::kUsb:
    case mojom::EventCategoryEnum::kTouchpad:
    case mojom::EventCategoryEnum::kHdmi:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         iterator->second);
      break;
    case mojom::EventCategoryEnum::kNetwork:
      event_subscriber.SubscribeToNetworkEvents();
      break;
    case mojom::EventCategoryEnum::kUnmappedEnumField:
      NOTREACHED();
      return EXIT_FAILURE;
  }

  std::cout << "Subscribe to " << FLAGS_category << " events successfully."
            << std::endl;

  // Schedule an exit after |FLAGS_length_seconds|.
  base::ThreadTaskRunnerHandle::Get()->PostDelayedTask(
      FROM_HERE, run_loop.QuitClosure(), base::Seconds(FLAGS_length_seconds));
  run_loop.Run();
  return EXIT_SUCCESS;
}

}  // namespace diagnostics
