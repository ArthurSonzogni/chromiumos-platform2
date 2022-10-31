// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/request.h"

#include <iostream>

#include "backend/test/backend_test_utils.h"

namespace cros_im {
namespace test {

namespace {

std::string RequestTypeToString(Request::RequestType type) {
  switch (type) {
    case Request::kCreateTextInput:
      return "create_text_input()";
    case Request::kDestroy:
      return "destroy()";
    case Request::kActivate:
      return "activate()";
    case Request::kDeactivate:
      return "deactivate()";
    case Request::kShowInputPanel:
      return "show_input_panel()";
    case Request::kHideInputPanel:
      return "hide_input_panel()";
    case Request::kReset:
      return "reset()";
    case Request::kSetSurroundingText:
      return "set_surrounding_text()";
    case Request::kSetContentType:
      return "set_content_type()";
    case Request::kSetCursorRectangle:
      return "set_cursor_rectangle()";
    case Request::kExtensionDestroy:
      return "extension_destroy()";
  }
  return "[invalid request]";
}

}  // namespace

Request::~Request() = default;

bool Request::RequestMatches(const Request& actual) const {
  return text_input_id_ == actual.text_input_id_ && type_ == actual.type_;
}

void Request::Print(std::ostream& stream) const {
  stream << RequestTypeToString(type_);
}

std::ostream& operator<<(std::ostream& stream, const Request& request) {
  stream << "[Request<" << request.text_input_id_ << ">: ";
  request.Print(stream);
  stream << "]";
  return stream;
}

SetContentTypeRequest::SetContentTypeRequest(int text_input_id,
                                             uint32_t hints,
                                             uint32_t purpose)
    : Request(text_input_id, Request::kSetContentType),
      hints_(hints),
      purpose_(purpose) {}

SetContentTypeRequest::~SetContentTypeRequest() = default;

bool SetContentTypeRequest::RequestMatches(const Request& actual) const {
  if (!Request::RequestMatches(actual))
    return false;
  const SetContentTypeRequest* other =
      dynamic_cast<const SetContentTypeRequest*>(&actual);
  if (!other) {
    FAILED() << "SetContentType request was not of type SetContentTypeRequest";
    return false;
  }

  return hints_ == other->hints_ && purpose_ == other->purpose_;
}

void SetContentTypeRequest::Print(std::ostream& stream) const {
  stream << "set_content_type(hints = " << hints_ << ", purpose = " << purpose_
         << ")";
}

SetSurroundingTextRequest::SetSurroundingTextRequest(int text_input_id,
                                                     const std::string& text,
                                                     uint32_t cursor,
                                                     uint32_t anchor)
    : Request(text_input_id, Request::kSetSurroundingText),
      text_(text),
      cursor_(cursor),
      anchor_(anchor) {}

SetSurroundingTextRequest::~SetSurroundingTextRequest() = default;

bool SetSurroundingTextRequest::RequestMatches(const Request& actual) const {
  if (!Request::RequestMatches(actual))
    return false;
  const SetSurroundingTextRequest* other =
      dynamic_cast<const SetSurroundingTextRequest*>(&actual);
  if (!other) {
    FAILED() << "SetSurroundingText request was not of type "
                "SetSurroundingTextRequest";
    return false;
  }

  return text_ == other->text_ && cursor_ == other->cursor_ &&
         anchor_ == other->anchor_;
}

void SetSurroundingTextRequest::Print(std::ostream& stream) const {
  stream << "set_surrounding_text(text = " << text_ << ", cursor = " << cursor_
         << ", anchor = " << anchor_ << ")";
}
}  // namespace test
}  // namespace cros_im
