// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "missive/client/report_queue_provider_test_helper.h"

#include "missive/client/mock_report_queue_provider.h"
#include "missive/client/report_queue_provider.h"

namespace reporting {

namespace report_queue_provider_test_helper {

static MockReportQueueProvider* g_mock_report_queue_provider = nullptr;

void SetForTesting(MockReportQueueProvider* provider) {
  g_mock_report_queue_provider = provider;
}

}  // namespace report_queue_provider_test_helper

// Implementation of the mock report provider for this test helper.
ReportQueueProvider* ReportQueueProvider::GetInstance() {
  return report_queue_provider_test_helper::g_mock_report_queue_provider;
}

}  // namespace reporting
