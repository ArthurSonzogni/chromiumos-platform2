// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef PATCHPANEL_MOCK_CONNMARK_UPDATER_H_
#define PATCHPANEL_MOCK_CONNMARK_UPDATER_H_

#include "patchpanel/connmark_updater.h"

#include <gmock/gmock.h>

namespace patchpanel {

class MockConnmarkUpdater : public ConnmarkUpdater {
 public:
  explicit MockConnmarkUpdater(ConntrackMonitor* monitor);
  explicit MockConnmarkUpdater(const ConnmarkUpdater&) = delete;
  MockConnmarkUpdater& operator=(const ConnmarkUpdater&) = delete;
  ~MockConnmarkUpdater();

  MOCK_METHOD(void,
              UpdateConnmark,
              (const Conntrack5Tuple& conn, Fwmark mark, Fwmark mask),
              (override));
};

}  // namespace patchpanel

#endif  // PATCHPANEL_MOCK_CONNMARK_UPDATER_H_
