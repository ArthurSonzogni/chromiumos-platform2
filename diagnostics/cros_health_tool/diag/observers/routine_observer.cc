// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_health_tool/diag/observers/routine_observer.h"

#include <iostream>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/functional/callback_forward.h>

namespace diagnostics {

namespace {

namespace mojom = ::ash::cros_healthd::mojom;

void PrintMemoryDetail(const mojom::MemoryRoutineDetailPtr& memory_detail) {
  std::cout << ("Bytes: ") << memory_detail->bytes_tested << std::endl;
  if (!memory_detail->result.is_null()) {
    for (const auto& test : memory_detail->result->passed_items) {
      std::cout << ("Passed Tests: ") << test << std::endl;
    }
    for (const auto& test : memory_detail->result->failed_items) {
      std::cout << ("Failed Tests: ") << test << std::endl;
    }
  }
}

}  // namespace

RoutineObserver::RoutineObserver(base::OnceClosure quit_closure)
    : receiver_{this /* impl */}, quit_closure_{std::move(quit_closure)} {}

RoutineObserver::~RoutineObserver() = default;

void RoutineObserver::OnRoutineStateChange(
    mojom::RoutineStatePtr state_update) {
  switch (state_update->state_union->which()) {
    case mojom::RoutineStateUnion::Tag::kFinished: {
      auto& finished_state = state_update->state_union->get_finished();
      std::cout << '\r' << "Running Progress: " << int(state_update->percentage)
                << std::endl;
      std::string passed_status =
          finished_state->has_passed ? "Passed" : "Failed";
      std::cout << ("Status: ") << passed_status << std::endl;
      switch (finished_state->detail->which()) {
        case mojom::RoutineDetail::Tag::kMemory:
          PrintMemoryDetail(finished_state->detail->get_memory());
          break;
      }
      std::move(quit_closure_).Run();
      return;
    }
    case mojom::RoutineStateUnion::Tag::kInitialized: {
      std::cout << "Initialized" << std::endl;
      return;
    }
    case mojom::RoutineStateUnion::Tag::kWaiting: {
      std::cout << '\r' << "Waiting: "
                << state_update->state_union->get_waiting()->reason
                << std::endl;
      return;
    }
    case mojom::RoutineStateUnion::Tag::kRunning: {
      std::cout << '\r' << "Running Progress: " << int(state_update->percentage)
                << std::flush;
      return;
    }
  }
}

}  // namespace diagnostics
