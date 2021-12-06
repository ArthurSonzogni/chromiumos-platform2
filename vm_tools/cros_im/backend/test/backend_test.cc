// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

#include <glib.h>

#include "backend/test/backend_test_utils.h"

namespace cros_im {
namespace test {

namespace {

int OnIdle(void* data) {
  static_cast<BackendTest*>(data)->RunNextEvent();
  // Disconnect this signal.
  return G_SOURCE_REMOVE;
}

}  // namespace

std::map<std::string, BackendTest::TestInitializer>
    BackendTest::test_initializers_;

std::ostream& operator<<(std::ostream& stream, const Action& action) {
  if (action.is_request_) {
    stream << *action.request_;
  } else {
    stream << *action.event_;
  }
  return stream;
}

BackendTest* BackendTest::GetInstance() {
  static BackendTest instance;

  if (!instance.initialized_) {
    instance.initialized_ = true;
    std::string test_name = getenv("CROS_TEST_FULL_NAME");
    auto it = test_initializers_.find(test_name);
    if (it == test_initializers_.end()) {
      FAILED() << "Could not find test spec for test '" << test_name << "'.";
    } else {
      // Call the matched SetUpExpectations().
      (instance.*(it->second))();
    }
  }

  return &instance;
}

void BackendTest::ProcessRequest(const Request& request) {
  for (const auto& ignore : ignored_requests_) {
    if (ignore->RequestMatches(request)) {
      return;
    }
  }

  if (actions_.empty()) {
    FAILED() << "Received request " << request
             << " but no expectations were left";
    return;
  }

  if (!actions_.front().is_request_ ||
      !actions_.front().request_->RequestMatches(request)) {
    FAILED() << "Received request " << request << " did not match next action "
             << actions_.front();
    return;
  }
  actions_.pop();

  PostEventIfNeeded();
}

void BackendTest::RunNextEvent() {
  actions_.front().event_->Run();
  actions_.pop();
  PostEventIfNeeded();
}

void BackendTest::Ignore(Request::RequestType request_type) {
  ignored_requests_.push_back(std::make_unique<Request>(request_type));
}

void BackendTest::Expect(Request::RequestType request_type) {
  actions_.emplace(std::make_unique<Request>(request_type));
}

void BackendTest::SendCommitString(const std::string& string) {
  actions_.emplace(std::make_unique<CommitStringEvent>(string));
}

void BackendTest::PostEventIfNeeded() {
  if (actions_.empty() || actions_.front().is_request_)
    return;
  // This only applies when running with a GTK frontend and we'll need different
  // logic when we add a XIM server.
  g_idle_add(OnIdle, this);
}

}  // namespace test
}  // namespace cros_im
