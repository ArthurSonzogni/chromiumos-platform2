// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MIST_MOCK_UDEV_ENUMERATE_H_
#define MIST_MOCK_UDEV_ENUMERATE_H_

#include <base/compiler_specific.h>
#include <gmock/gmock.h>

#include "mist/udev_enumerate.h"

namespace mist {

class MockUdevEnumerate : public UdevEnumerate {
 public:
  MockUdevEnumerate();
  virtual ~MockUdevEnumerate() OVERRIDE;

  MOCK_METHOD1(AddMatchSubsystem, bool(const char* subsystem));
  MOCK_METHOD1(AddNoMatchSubsystem, bool(const char* subsystem));
  MOCK_METHOD2(AddMatchSysAttribute,
               bool(const char* attribute, const char* value));
  MOCK_METHOD2(AddNoMatchSysAttribute,
               bool(const char* attribute, const char* value));
  MOCK_METHOD2(AddMatchProperty, bool(const char* property, const char* value));
  MOCK_METHOD1(AddMatchSysName, bool(const char* sys_name));
  MOCK_METHOD1(AddMatchTag, bool(const char* tag));
  MOCK_METHOD0(AddMatchIsInitialized, bool());
  MOCK_METHOD1(AddSysPath, bool(const char* sys_path));
  MOCK_METHOD0(ScanDevices, bool());
  MOCK_METHOD0(ScanSubsystems, bool());
  MOCK_CONST_METHOD0(GetListEntry, UdevListEntry*());

 private:
  DISALLOW_COPY_AND_ASSIGN(MockUdevEnumerate);
};

}  // namespace mist

#endif  // MIST_MOCK_UDEV_ENUMERATE_H_
