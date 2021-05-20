// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LIBHWSEC_FOUNDATION_ERROR_ERROR_MESSAGE_H_
#define LIBHWSEC_FOUNDATION_ERROR_ERROR_MESSAGE_H_

#include <iostream>
#include <memory>
#include <string>
#include <utility>

#include "libhwsec-foundation/error/error.h"

/* A generic message error object, useful for wrapping error with some message.
 *
 * Example usage:
 * if (auto err = SomethingReturnError()) {
 *   return CreateErrorWrap<Error>(std::move(err), "failed something");
 * }
 */

namespace hwsec_foundation {
namespace error {

class ErrorObj : public ErrorBaseObj {
 public:
  explicit ErrorObj(const std::string& error_message)
      : error_message_(error_message) {}
  explicit ErrorObj(std::string&& error_message)
      : error_message_(std::move(error_message)) {}
  virtual ~ErrorObj() = default;

  hwsec_foundation::error::ErrorBase SelfCopy() const {
    return std::make_unique<ErrorObj>(error_message_);
  }

  std::string ToReadableString() const { return error_message_; }

 protected:
  ErrorObj(ErrorObj&&) = default;

 private:
  const std::string error_message_;
};
using Error = std::unique_ptr<ErrorObj>;

}  // namespace error
}  // namespace hwsec_foundation

#endif  // LIBHWSEC_FOUNDATION_ERROR_ERROR_MESSAGE_H_
