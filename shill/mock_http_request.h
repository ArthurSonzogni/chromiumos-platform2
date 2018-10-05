// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_HTTP_REQUEST_H_
#define SHILL_MOCK_HTTP_REQUEST_H_

#include <memory>

#include <base/macros.h>
#include <gmock/gmock.h>

#include "shill/http_request.h"
#include "shill/http_url.h"  // MOCK_METHOD3() call below needs sizeof(HttpUrl).

namespace shill {

class MockHttpRequest : public HttpRequest {
 public:
  explicit MockHttpRequest(ConnectionRefPtr connection);
  ~MockHttpRequest() override;

  MOCK_METHOD3(
      Start,
      HttpRequest::Result(
          const HttpUrl& url,
          const base::Callback<void(std::shared_ptr<brillo::http::Response>)>&
              read_event_callback,
          const base::Callback<void(Result)>& result_callback));
  MOCK_METHOD0(Stop, void());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockHttpRequest);
};

}  // namespace shill

#endif  // SHILL_MOCK_HTTP_REQUEST_H_
