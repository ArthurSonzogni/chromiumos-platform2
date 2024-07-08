// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/cros_healthd/routines/interactive_routine_control.h"

#include <string>
#include <utility>

#include <base/check.h>

#include "diagnostics/mojom/public/cros_healthd_routines.mojom.h"

namespace {

namespace mojom = ash::cros_healthd::mojom;

bool ReplyMatchesInquiry(const mojom::RoutineStatePtr& state,
                         const mojom::RoutineInquiryReplyPtr& reply) {
  if (!state->state_union->is_waiting()) {
    return false;
  }
  const auto& waiting = state->state_union->get_waiting();
  if (waiting->interaction.is_null() || !waiting->interaction->is_inquiry()) {
    return false;
  }
  const auto& inquiry = waiting->interaction->get_inquiry();
  switch (reply->which()) {
    case mojom::RoutineInquiryReply::Tag::kCheckLedLitUpState:
      return inquiry->is_check_led_lit_up_state();
    case mojom::RoutineInquiryReply::Tag::kUnplugAcAdapter:
      return inquiry->is_unplug_ac_adapter_inquiry();
    case mojom::RoutineInquiryReply::Tag::kCheckKeyboardBacklightState:
      return inquiry->is_check_keyboard_backlight_state();
    case mojom::RoutineInquiryReply::Tag::kUnrecognizedReply:
      return false;
  }
}

}  // namespace

namespace diagnostics {

InteractiveRoutineControl::InteractiveRoutineControl() = default;

InteractiveRoutineControl::~InteractiveRoutineControl() = default;

void InteractiveRoutineControl::SetWaitingInquiryState(
    const std::string& message, mojom::RoutineInquiryPtr inquiry) {
  CHECK(state()->state_union->is_running())
      << "Can only set waiting state from running state";
  BaseRoutineControl::mutable_state()->state_union =
      mojom::RoutineStateUnion::NewWaiting(mojom::RoutineStateWaiting::New(
          /*reason=*/mojom::RoutineStateWaiting::Reason::kWaitingInteraction,
          message, mojom::RoutineInteraction::NewInquiry(std::move(inquiry))));

  BaseRoutineControl::NotifyObserver();
}

void InteractiveRoutineControl::ReplyInquiry(
    mojom::RoutineInquiryReplyPtr reply) {
  if (!ReplyMatchesInquiry(state(), reply)) {
    RaiseException("Reply does not match the inquiry");
    return;
  }
  // Set running state before calling `OnReplyInquiry()` since
  // `OnReplyInquiry()` might call `RaiseException()`, which will destruct this
  // routine object.
  SetRunningState();
  OnReplyInquiry(std::move(reply));
}

}  // namespace diagnostics
