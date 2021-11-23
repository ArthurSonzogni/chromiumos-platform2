// Copyright 2021 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef MISSIVE_CLIENT_MOCK_REPORT_QUEUE_H_
#define MISSIVE_CLIENT_MOCK_REPORT_QUEUE_H_

#include <memory>

#include <base/callback.h>
#include <gmock/gmock.h>
#include <google/protobuf/message_lite.h>

#include "missive/client/report_queue.h"
#include "missive/proto/record.pb.h"
#include "missive/proto/record_constants.pb.h"
#include "missive/util/status.h"

namespace reporting {

// A mock of ReportQueue for use in testing.
class MockReportQueue : public ReportQueue {
 public:
  MockReportQueue();
  ~MockReportQueue() override;

  MOCK_METHOD(void,
              AddRecord,
              (base::StringPiece, Priority, ReportQueue::EnqueueCallback),
              (const override));

  MOCK_METHOD(void, Flush, (Priority, ReportQueue::FlushCallback), (override));

  MOCK_METHOD(
      (base::OnceCallback<void(StatusOr<std::unique_ptr<ReportQueue>>)>),
      PrepareToAttachActualQueue,
      (),
      (const override));
};

}  // namespace reporting

#endif  // MISSIVE_CLIENT_MOCK_REPORT_QUEUE_H_
