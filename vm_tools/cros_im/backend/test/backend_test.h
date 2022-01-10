// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_TEST_BACKEND_TEST_H_
#define VM_TOOLS_CROS_IM_BACKEND_TEST_BACKEND_TEST_H_

#include <map>
#include <memory>
#include <queue>
#include <string>
#include <utility>
#include <vector>

#include "backend/test/event.h"
#include "backend/test/request.h"

namespace cros_im {
namespace test {

// BACKEND_TEST(Group, Name) { .. } defines a function to initialize a
// BackendTest object with Requests to expect and Events to fire when running
// the matching test. The environment variable CROS_TEST_FULL_NAME should be
// set to Group.Name.

// In creating a backend test specification, it may be helpful to use the
// non-test IM module with WAYLAND_DEBUG=1, for example:
// $ export GTK_IM_MODULE=cros
// $ WAYLAND_DEBUG=1 ./cros_im_tests --gtest_filter=Group.Name 2>&1 | grep zwp

#define BACKEND_TEST(Group, Name)                                          \
  struct _##Group##_##Name {};                                             \
  template <>                                                              \
  void BackendTest::SetUpExpectations<_##Group##_##Name>();                \
  static int _ignore_##Group##_##Name =                                    \
      (BackendTest::RegisterTest<_##Group##_##Name>(#Group "." #Name), 0); \
  template <>                                                              \
  void BackendTest::SetUpExpectations<_##Group##_##Name>()

struct Action {
  explicit Action(std::unique_ptr<Request> request)
      : is_request_(true), request_(std::move(request)) {}
  explicit Action(std::unique_ptr<Event> event)
      : is_request_(false), event_(std::move(event)) {}

  const bool is_request_;
  std::unique_ptr<Request> request_;
  std::unique_ptr<Event> event_;
};

std::ostream& operator<<(std::ostream& stream, const Action& action);

// TODO(timloh): Check there are no remaining expectations on exit.
class BackendTest {
 public:
  BackendTest() = default;
  ~BackendTest() = default;

  static BackendTest* GetInstance();
  void ProcessRequest(const Request& request);

  // Called by BACKEND_TEST().
  template <class T>
  static void RegisterTest(const char* name) {
    test_initializers_[name] = &BackendTest::SetUpExpectations<T>;
  }

  void RunNextEvent();

 private:
  // Each BACKEND_TEST() macro invocation defines a specialization.
  template <class T>
  void SetUpExpectations();

  // For use in BACKEND_TEST().
  void Ignore(Request::RequestType type);
  void Expect(Request::RequestType type);
  void SendCommitString(const std::string& string);
  void SendKeySym(int keysym);

  // If the next action is an event, run it asynchronously.
  void PostEventIfNeeded();

  bool initialized_ = false;
  std::vector<std::unique_ptr<Request>> ignored_requests_;
  std::queue<Action> actions_;

  using TestInitializer = void (BackendTest::*)();
  static std::map<std::string, TestInitializer> test_initializers_;
};

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_TEST_BACKEND_TEST_H_
