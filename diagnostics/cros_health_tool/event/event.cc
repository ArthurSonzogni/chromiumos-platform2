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
#include <base/run_loop.h>
#include <base/threading/thread_task_runner_handle.h>
#include <base/time/time.h>
#include <brillo/flag_helper.h>

#include "diagnostics/cros_health_tool/event/event_subscriber.h"
#include "diagnostics/mojom/public/cros_healthd_events.mojom.h"

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

enum class EventCategory {
  kLid,
  kPower,
  kBluetooth,
  kNetwork,
  kAudio,
  kThunderbolt,
  kUsb,
  kAudioJack,
  kSdCard,
};

constexpr std::pair<const char*, EventCategory> kCategorySwitches[] = {
    {"lid", EventCategory::kLid},
    {"power", EventCategory::kPower},
    {"bluetooth", EventCategory::kBluetooth},
    {"network", EventCategory::kNetwork},
    {"audio", EventCategory::kAudio},
    {"thunderbolt", EventCategory::kThunderbolt},
    {"usb", EventCategory::kUsb},
    {"audio_jack", EventCategory::kAudioJack},
    {"sd_card", EventCategory::kSdCard},
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

  std::map<std::string, EventCategory> switch_to_category(
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
    case EventCategory::kLid:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kLid);
      break;
    case EventCategory::kPower:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kPower);
      break;
    case EventCategory::kBluetooth:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kBluetooth);
      break;
    case EventCategory::kNetwork:
      event_subscriber.SubscribeToNetworkEvents();
      break;
    case EventCategory::kAudio:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kAudio);
      break;
    case EventCategory::kThunderbolt:
      event_subscriber.SubscribeToEvents(
          run_loop.QuitClosure(), mojom::EventCategoryEnum::kThunderbolt);
      break;
    case EventCategory::kUsb:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kUsb);
      break;
    case EventCategory::kAudioJack:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kAudioJack);
      break;
    case EventCategory::kSdCard:
      event_subscriber.SubscribeToEvents(run_loop.QuitClosure(),
                                         mojom::EventCategoryEnum::kSdCard);
      break;
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
