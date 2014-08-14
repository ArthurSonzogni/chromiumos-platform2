// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CONNECTION_INFO_READER_H_
#define SHILL_MOCK_CONNECTION_INFO_READER_H_

#include <vector>

#include <base/basictypes.h>
#include <gmock/gmock.h>

#include "shill/connection_info_reader.h"

namespace shill {

class ConnectionInfo;

class MockConnectionInfoReader : public ConnectionInfoReader {
 public:
  MockConnectionInfoReader();
  ~MockConnectionInfoReader() override;

  MOCK_METHOD1(LoadConnectionInfo,
               bool(std::vector<ConnectionInfo> *info_list));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockConnectionInfoReader);
};

}  // namespace shill

#endif  // SHILL_MOCK_CONNECTION_INFO_READER_H_
