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

#include "rmad/constants.h"
#include "rmad/proto_bindings/rmad.pb.h"

namespace rmad {

namespace {

const base::FilePath kDefaultJsonStoreFilePath("/var/lib/rmad/state");
const RmadState::StateCase kInitialStateCase = RmadState::kWelcome;

}  // namespace

RmadInterfaceImpl::RmadInterfaceImpl() : RmadInterface() {
  json_store_ = base::MakeRefCounted<JsonStore>(kDefaultJsonStoreFilePath);
  state_handler_manager_ = std::make_unique<StateHandlerManager>(json_store_);
  state_handler_manager_->RegisterStateHandlers();
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
  return json_store_->SetValue(kStateHistory, state_history);
}

void RmadInterfaceImpl::Initialize() {
  // Initialize |current state_|, |state_history_|, and |allow_abort_| flag.
  current_state_case_ = RmadState::STATE_NOT_SET;
  state_history_.clear();
  allow_abort_ = true;
  if (json_store_->GetReadError() != JsonStore::READ_ERROR_NO_SUCH_FILE) {
    if (std::vector<int> state_history;
        json_store_->GetReadError() == JsonStore::READ_ERROR_NONE &&
        json_store_->GetValue(kStateHistory, &state_history) &&
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
      // TODO(gavindodd): Set to an error state or send a signal to Chrome that
      // the RMA was reset so a message can be displayed.
      if (!StoreStateHistory()) {
        LOG(ERROR) << "Could not store initial state";
        // TODO(gavindodd): Set to an error state or send a signal to Chrome
        // that the json store failed so a message can be displayed.
      }
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

RmadErrorCode RmadInterfaceImpl::GetInitializedStateHandler(
    RmadState::StateCase state_case,
    scoped_refptr<BaseStateHandler>* state_handler) const {
  auto handler = state_handler_manager_->GetStateHandler(state_case);
  if (!handler) {
    LOG(INFO) << "No registered state handler for state " << state_case;
    return RMAD_ERROR_STATE_HANDLER_MISSING;
  }
  if (RmadErrorCode init_error = handler->InitializeState();
      init_error != RMAD_ERROR_OK) {
    LOG(INFO) << "Failed to initialize current state " << state_case;
    return init_error;
  }
  *state_handler = handler;
  return RMAD_ERROR_OK;
}

void RmadInterfaceImpl::GetCurrentState(const GetStateCallback& callback) {
  GetStateReply reply;
  scoped_refptr<BaseStateHandler> state_handler;

  if (RmadErrorCode error =
          GetInitializedStateHandler(current_state_case_, &state_handler);
      error == RMAD_ERROR_OK) {
    LOG(INFO) << "Get current state succeeded: " << current_state_case_;
    reply.set_error(RMAD_ERROR_OK);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
    reply.set_can_go_back(CanGoBack());
  } else {
    if (error == RMAD_ERROR_STATE_HANDLER_MISSING) {
      reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
    } else {
      reply.set_error(error);
    }
  }

  callback.Run(reply);
}

void RmadInterfaceImpl::TransitionNextState(
    const TransitionNextStateRequest& request,
    const GetStateCallback& callback) {
  // TODO(chenghan): Add error replies when failed to write |json_store_|.
  GetStateReply reply;
  scoped_refptr<BaseStateHandler> state_handler, next_state_handler;

  if (RmadErrorCode error =
          GetInitializedStateHandler(current_state_case_, &state_handler);
      error != RMAD_ERROR_OK) {
    reply.set_error(error);
    callback.Run(reply);
    return;
  }

  auto [next_state_case_error, next_state_case] =
      state_handler->GetNextStateCase(request.state());
  if (next_state_case_error != RMAD_ERROR_OK) {
    LOG(INFO) << "Transitioning to next state rejected by state "
              << current_state_case_;
    CHECK(next_state_case == current_state_case_)
        << "State transition should not happen with errors.";
    reply.set_error(next_state_case_error);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
    reply.set_can_go_back(CanGoBack());
    callback.Run(reply);
    return;
  }

  // TODO(chenghan): What about states waiting for signals?
  CHECK(next_state_case != current_state_case_)
      << "Staying at the same state without errors.";

  if (RmadErrorCode error =
          GetInitializedStateHandler(next_state_case, &next_state_handler);
      error != RMAD_ERROR_OK) {
    reply.set_error(error);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
    reply.set_can_go_back(CanGoBack());
    callback.Run(reply);
    return;
  }

  // Transition to next state.
  LOG(INFO) << "Transition to next state succeeded: from "
            << current_state_case_ << " to " << next_state_case;
  // Append next state to stack.
  state_history_.push_back(next_state_case);
  if (!StoreStateHistory()) {
    LOG(ERROR) << "Could not store history";
  }
  // Update state.
  current_state_case_ = next_state_case;
  if (allow_abort_ && !state_handler->IsRepeatable()) {
    // This is a one-way transition. |allow_abort| cannot go from false to
    // true, unless we restart the whole RMA process.
    allow_abort_ = false;
  }

  reply.set_error(RMAD_ERROR_OK);
  reply.set_allocated_state(new RmadState(next_state_handler->GetState()));
  reply.set_can_go_back(CanGoBack());
  callback.Run(reply);
}

void RmadInterfaceImpl::TransitionPreviousState(
    const GetStateCallback& callback) {
  GetStateReply reply;
  scoped_refptr<BaseStateHandler> state_handler, prev_state_handler;

  if (RmadErrorCode error =
          GetInitializedStateHandler(current_state_case_, &state_handler);
      error != RMAD_ERROR_OK) {
    reply.set_error(error);
    callback.Run(reply);
    return;
  }

  if (!CanGoBack()) {
    LOG(INFO) << "Not allowed to go back to previous state";
    reply.set_error(RMAD_ERROR_TRANSITION_FAILED);
    reply.set_allocated_state(new RmadState(state_handler->GetState()));
    reply.set_can_go_back(false);
    callback.Run(reply);
    return;
  }

  RmadState::StateCase prev_state_case = *std::prev(state_history_.end(), 2);
  if (RmadErrorCode error =
          GetInitializedStateHandler(prev_state_case, &prev_state_handler);
      error != RMAD_ERROR_OK) {
    reply.set_error(error);
    callback.Run(reply);
    return;
  }

  // Transition to previous state.
  LOG(INFO) << "Transition to previous state succeeded: from "
            << current_state_case_ << " to " << prev_state_case;
  // Remove current state from stack.
  state_history_.pop_back();
  if (!StoreStateHistory()) {
    LOG(ERROR) << "Could not store history";
  }
  // Update state.
  current_state_case_ = prev_state_case;

  reply.set_error(RMAD_ERROR_OK);
  reply.set_allocated_state(new RmadState(prev_state_handler->GetState()));
  reply.set_can_go_back(CanGoBack());
  callback.Run(reply);
}

void RmadInterfaceImpl::AbortRma(const AbortRmaCallback& callback) {
  AbortRmaReply reply;

  if (allow_abort_) {
    LOG(INFO) << "AbortRma: Abort allowed.";
    // TODO(gavindodd): The json store file should be deleted and a reboot
    // triggered.
    json_store_->Clear();
    current_state_case_ = RmadState::STATE_NOT_SET;
    reply.set_error(RMAD_ERROR_RMA_NOT_REQUIRED);
  } else {
    LOG(INFO) << "AbortRma: Failed to abort.";
    reply.set_error(RMAD_ERROR_ABORT_FAILED);
  }

  callback.Run(reply);
}

void RmadInterfaceImpl::GetLogPath(const GetLogPathCallback& callback) {
  // TODO(gavindodd): Return a valid log file path and add tests.
  std::string path = "";
  callback.Run(path);
}

bool RmadInterfaceImpl::CanGoBack() const {
  if (state_history_.size() > 1) {
    const auto current_state_handler =
        state_handler_manager_->GetStateHandler(state_history_.back());
    const auto prev_state_handler = state_handler_manager_->GetStateHandler(
        *std::prev(state_history_.end(), 2));
    CHECK(current_state_handler);
    CHECK(prev_state_handler);
    return (current_state_handler->IsRepeatable() &&
            prev_state_handler->IsRepeatable());
  }
  return false;
}

}  // namespace rmad
