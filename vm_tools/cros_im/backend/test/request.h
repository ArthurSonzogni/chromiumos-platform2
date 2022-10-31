// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_
#define VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_

#include <iostream>
#include <string>

namespace cros_im {
namespace test {

// Represents a Wayland request, i.e. a call to the compositor.
class Request {
 public:
  enum RequestType {
    // Requests on the text_input_manager
    kCreateTextInput,
    // Requests on a text_input object
    kDestroy,
    kActivate,
    kDeactivate,
    kShowInputPanel,
    kHideInputPanel,
    kReset,
    kSetSurroundingText,
    kSetContentType,
    kSetCursorRectangle,
    // Requests on a text_input_extension object
    kExtensionDestroy,
  };

  Request(int text_input_id, RequestType type)
      : text_input_id_(text_input_id), type_(type) {}
  virtual ~Request();
  virtual bool RequestMatches(const Request& actual) const;
  virtual void Print(std::ostream& stream) const;

 private:
  friend std::ostream& operator<<(std::ostream& stream, const Request& request);
  int text_input_id_;
  RequestType type_;
};

std::ostream& operator<<(std::ostream& stream, const Request& request);

class SetContentTypeRequest : public Request {
 public:
  SetContentTypeRequest(int text_input_id, uint32_t hints, uint32_t purpose);
  ~SetContentTypeRequest() override;
  bool RequestMatches(const Request& actual) const override;
  void Print(std::ostream& stream) const override;

 private:
  uint32_t hints_;
  uint32_t purpose_;
};

class SetSurroundingTextRequest : public Request {
 public:
  SetSurroundingTextRequest(int text_input_id,
                            const std::string& text,
                            uint32_t cursor,
                            uint32_t anchor);
  ~SetSurroundingTextRequest() override;
  bool RequestMatches(const Request& actual) const override;
  void Print(std::ostream& stream) const override;

 private:
  std::string text_;
  uint32_t cursor_;
  uint32_t anchor_;
};

}  // namespace test
}  // namespace cros_im

#endif  // VM_TOOLS_CROS_IM_BACKEND_TEST_REQUEST_H_
