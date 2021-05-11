// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "rmad/rmad_interface_impl.h"

#include <memory>
#include <string>
#include <utility>

#include <base/check.h>
#include <base/logging.h>
#include <base/memory/scoped_refptr.h>
#include <base/values.h>

#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

namespace {

const base::FilePath kDefaultJsonStoreFilePath("/var/lib/rmad/state");
constexpr char kRmadStateHistory[] = "state_history";
const RmadState::StateCase kInitialStateCase = RmadState::kWelcome;

}  // namespace

RmadInterfaceImpl::RmadInterfaceImpl() : RmadInterface() {
  json_store_ = base::MakeRefCounted<JsonStore>(kDefaultJsonStoreFilePath);
  state_handler_manager_ = std::make_unique<StateHandlerManager>(json_store_);
  state_handler_manager_->InitializeStateHandlers();
  Initialize();
}

RmadInterfaceImpl::RmadInterfaceImpl(
    scoped_refptr<JsonStore> json_store,
    std::unique_ptr<StateHandlerManager> state_handler_manager)
    : RmadInterface(),
      json_store_(json_store),
      state_handler_manager_(std::move(state_handler_manager)) {
  Initialize();
}

bool RmadInterfaceImpl::StoreStateHistory() {
  std::vector<int> state_history;
  for (auto s : state_history_) {
    state_history.push_back(RmadState::StateCase(s));
  }
  return json_store_->SetValue(kRmadStateHistory, state_history);
}

void RmadInterfaceImpl::Initialize() {
  // Initialize |current state_|, |state_history_|, and |allow_abort_| flag.
  current_state_case_ = RmadState::STATE_NOT_SET;
  state_history_.clear();
  allow_abort_ = true;
  if (json_store_->GetReadError() != JsonStore::READ_ERROR_NO_SUCH_FILE) {
    if (std::vector<int> state_history;
        json_store_->GetReadError() == JsonStore::READ_ERROR_NONE &&
        json_store_->GetValue(kRmadStateHistory, &state_history) &&
        state_history.size()) {
      for (int state : state_history) {
        // Reject any state that does not have a handler.
        if (RmadState::StateCase s = RmadState::StateCase(state);
            auto handler = state_handler_manager_->GetStateHandler(s)) {
          state_history_.push_back(s);
          if (!handler->IsRepeatable()) {
            allow_abort_ = false;
          }
        } else {
          // TODO(chenghan): Return to welcome screen with an error implying
          //                 a fatal file corruption.
          LOG(ERROR) << "Missing handler for state " << state << ".";
        }
      }
    }
    if (state_history_.size() > 0) {
      current_state_case_ = state_history_.back();
    } else {
      LOG(WARNING) << "Could not read state history from json store, reset to "
                      "initial state.";
      // TODO(gavindodd): Reset the json store so it is not read only.
      current_state_case_ = kInitialStateCase;
      state_history_.push_back(current_state_case_);
      StoreStateHistory();
      // TODO(gavindodd): Set to an error state or send a signal to Chrome that
      // the RMA was reset so a message can be displayed.
    }
  } else if (cr50_utils_.RoVerificationKeyPressed()) {
    current_state_case_ = kInitialStateCase;
    state_history_.push_back(current_state_case_);
    if (!StoreStateHistory()) {
      LOG(ERROR) << "Could not store initial state";
      // TODO(gavindodd): Set to an error state or send a signal to Chrome that
      // the json store failed so a message can be displayed.
    }
  }
}

void RmadInterfaceImpl::GetCurrentState(const GetStateCallback& callback) {
  GetStateReply reply;
  auto state_handler =
      state_handler_manager_->GetStateHandler(current_state_case_);
  if (state_handler) {
    reply.set_error(RmadErrorCode::RMAD_ERROR_OK);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
  } else {
    reply.set_error(RmadErrorCode::RMAD_ERROR_RMA_NOT_REQUIRED);
  }
  // TODO(chenghan): Set |can_go_back|.
  callback.Run(reply);
}

void RmadInterfaceImpl::TransitionNextState(
    const TransitionNextStateRequest& request,
    const GetStateCallback& callback) {
  // TODO(chenghan): Add error replies when failed to get `state_handler`, or
  //                 failed to write `json_store_`.
  auto state_handler =
      state_handler_manager_->GetStateHandler(current_state_case_);
  GetStateReply reply;

  if (state_handler) {
    const auto [error, next_state_case] =
        state_handler->GetNextStateCase(request.state());

    if (next_state_case != current_state_case_) {
      CHECK(error == RMAD_ERROR_OK)
          << "State transition should not happen when an error occurs";
      state_handler = state_handler_manager_->GetStateHandler(next_state_case);
      CHECK(state_handler) << "No registered state handler for state "
                           << next_state_case;
      current_state_case_ = next_state_case;
      state_history_.push_back(current_state_case_);
      StoreStateHistory();
      if (allow_abort_ && !state_handler->IsRepeatable()) {
        // This is a one-way transition. |allow_abort| cannot go from false to
        // true, unless we restart the whole RMA process.
        allow_abort_ = false;
      }
    } else {
      // Error messages are logged by state handlers. We only need to make sure
      // we don't stay in the same state without any reason.
      CHECK(error != RMAD_ERROR_OK) << "Failed to transition without an error";
    }

    reply.set_error(error);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
    // TODO(gavindodd): Set can go back by inspecting stack?
    // Add a 'repeatable' flag on state handlers?
    reply.set_can_go_back(state_history_.size() > 1);
  } else {
    // TODO(gavindodd): This is a pretty bad error. What next?
    reply.set_error(RmadErrorCode::RMAD_ERROR_REQUEST_INVALID);
  }
  // TODO(chenghan): Set |can_go_back|.

  callback.Run(reply);
}

void RmadInterfaceImpl::TransitionPreviousState(
    const GetStateCallback& callback) {
  GetStateReply reply;
  auto state_handler =
      state_handler_manager_->GetStateHandler(current_state_case_);
  CHECK(state_handler);
  if (state_history_.size() > 1) {
    auto prev_state_handler = state_handler_manager_->GetStateHandler(
        *std::prev(state_history_.end(), 2));
    CHECK(prev_state_handler);
    if (state_handler->IsRepeatable() && prev_state_handler->IsRepeatable()) {
      // Clear data from current state.
      state_handler->ResetState();
      // Remove current state from stack.
      state_history_.pop_back();
      StoreStateHistory();
      // Get new state.
      current_state_case_ = state_history_.back();
      // Update the state handler for the new state.
      state_handler = prev_state_handler;
      reply.set_error(RmadErrorCode::RMAD_ERROR_OK);
    } else {
      reply.set_error(RmadErrorCode::RMAD_ERROR_TRANSITION_FAILED);
    }
  } else {
    reply.set_error(RmadErrorCode::RMAD_ERROR_TRANSITION_FAILED);
  }
  // In all cases fetch whatever the current state is.
  reply.set_allocated_state(new RmadState(state_handler->GetState()));
  callback.Run(reply);
}

void RmadInterfaceImpl::AbortRma(const AbortRmaCallback& callback) {
  AbortRmaReply reply;

  if (allow_abort_) {
    DLOG(INFO) << "AbortRma: Abort allowed.";
    // TODO(gavindodd): The json store file should be deleted and a reboot
    // triggered.
    json_store_->Clear();
    current_state_case_ = RmadState::STATE_NOT_SET;
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
  } else {
    DLOG(INFO) << "AbortRma: Failed to abort.";
    reply.set_error(RMAD_ERROR_ABORT_FAILED);
  }

  callback.Run(reply);
}

void RmadInterfaceImpl::GetLogPath(const GetLogPathCallback& callback) {
  // TODO(gavindodd): Return a valid log file path and add tests.
  std::string path = "";
  callback.Run(path);
}

}  // namespace rmad
