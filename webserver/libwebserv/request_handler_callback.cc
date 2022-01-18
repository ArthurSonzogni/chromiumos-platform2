// Copyright 2015 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <libwebserv/request_handler_callback.h>

#include <utility>

namespace libwebserv {

RequestHandlerCallback::RequestHandlerCallback(
    base::RepeatingCallback<HandlerSignature> callback)
    : callback_(std::move(callback)) {}

void RequestHandlerCallback::HandleRequest(std::unique_ptr<Request> request,
                                           std::unique_ptr<Response> response) {
  callback_.Run(std::move(request), std::move(response));
}

}  // namespace libwebserv
