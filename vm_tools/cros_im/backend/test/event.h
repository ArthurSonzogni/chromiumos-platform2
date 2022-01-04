// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_TEST_EVENT_H_
#define VM_TOOLS_CROS_IM_BACKEND_TEST_EVENT_H_

#include <iostream>
#include <string>

namespace cros_im {
namespace test {

// Represents a Wayland event, i.e. a call from the compositor.
class Event {
 public:
  virtual ~Event() {}
  virtual void Run() const = 0;
  virtual void Print(std::ostream& stream) const = 0;
};

std::ostream& operator<<(std::ostream& stream, const Event& event);

class CommitStringEvent : public Event {
 public:
  explicit CommitStringEvent(const std::string& text) : text_(text) {}
  ~CommitStringEvent() override;
  void Run() const override;
  void Print(std::ostream& stream) const override;

 private:
  std::string text_;
};

class KeySymEvent : public Event {
 public:
  explicit KeySymEvent(int keysym) : keysym_(keysym) {}
  ~KeySymEvent() override;
  void Run() const override;
  void Print(std::ostream& stream) const override;

 private:
  int keysym_;
};

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_TEST_EVENT_H_
