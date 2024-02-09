// Copyright 2015 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_CROS_DBUS_TEST_UTILS_H_
#define UPDATE_ENGINE_CROS_DBUS_TEST_UTILS_H_

#include <memory>
#include <set>
#include <string>
#include <utility>

#include <base/functional/bind.h>
#include <brillo/message_loops/message_loop.h>
#include <gmock/gmock.h>

namespace chromeos_update_engine {
namespace dbus_test_utils {

#define MOCK_SIGNAL_HANDLER_EXPECT_SIGNAL_HANDLER(mock_signal_handler,         \
                                                  mock_proxy, signal)          \
  do {                                                                         \
    EXPECT_CALL((mock_proxy),                                                  \
                DoRegister##signal##SignalHandler(::testing::_, ::testing::_)) \
        .WillOnce(::chromeos_update_engine::dbus_test_utils::GrabCallbacks(    \
            &(mock_signal_handler)));                                          \
  } while (false)

template <typename T>
class MockSignalHandler {
 public:
  MockSignalHandler() = default;
  ~MockSignalHandler() {
    if (callback_connected_task_ != brillo::MessageLoop::kTaskIdNull)
      brillo::MessageLoop::current()->CancelTask(callback_connected_task_);
  }

  // Returns whether the signal handler is registered.
  bool IsHandlerRegistered() const { return signal_callback_ != nullptr; }

  const base::RepeatingCallback<T>& signal_callback() {
    return *signal_callback_.get();
  }

  void GrabCallbacks(
      const base::RepeatingCallback<T>& signal_callback,
      dbus::ObjectProxy::OnConnectedCallback* on_connected_callback) {
    signal_callback_.reset(new base::RepeatingCallback<T>(signal_callback));
    on_connected_callback_.reset(new dbus::ObjectProxy::OnConnectedCallback(
        std::move(*on_connected_callback)));
    // Notify from the main loop that the callback was connected.
    callback_connected_task_ = brillo::MessageLoop::current()->PostTask(
        FROM_HERE, base::BindOnce(&MockSignalHandler<T>::OnCallbackConnected,
                                  base::Unretained(this)));
  }

 private:
  void OnCallbackConnected() {
    callback_connected_task_ = brillo::MessageLoop::kTaskIdNull;
    std::move(*on_connected_callback_).Run("", "", true);
  }

  brillo::MessageLoop::TaskId callback_connected_task_{
      brillo::MessageLoop::kTaskIdNull};

  std::unique_ptr<base::RepeatingCallback<T>> signal_callback_;
  std::unique_ptr<dbus::ObjectProxy::OnConnectedCallback>
      on_connected_callback_;
};

// Defines the action that will call MockSignalHandler<T>::GrabCallbacks for the
// right type.
ACTION_P(GrabCallbacks, mock_signal_handler) {
  mock_signal_handler->GrabCallbacks(arg0, arg1);
}

}  // namespace dbus_test_utils
}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_CROS_DBUS_TEST_UTILS_H_
