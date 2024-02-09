// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <brillo/message_loops/fake_message_loop.h>

#include "update_engine/common/mock_http_fetcher.h"
#include "update_engine/common/test_utils.h"
#include "update_engine/cros/fake_system_state.h"
#include "update_engine/cros/omaha_request_action.h"

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOGGING_FATAL); }
};

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  brillo::FakeMessageLoop loop(nullptr);
  loop.SetAsCurrent();

  chromeos_update_engine::FakeSystemState::CreateInstance();
  auto omaha_request_action =
      std::make_unique<chromeos_update_engine::OmahaRequestAction>(
          nullptr,
          std::make_unique<chromeos_update_engine::MockHttpFetcher>(data, size,
                                                                    nullptr),
          false, "" /* session_id */);
  auto collector_action =
      std::make_unique<chromeos_update_engine::ObjectCollectorAction<
          chromeos_update_engine::OmahaResponse>>();
  BondActions(omaha_request_action.get(), collector_action.get());
  chromeos_update_engine::ActionProcessor action_processor;
  action_processor.EnqueueAction(std::move(omaha_request_action));
  action_processor.EnqueueAction(std::move(collector_action));
  action_processor.StartProcessing();

  loop.Run();
  return 0;
}
