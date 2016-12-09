// Copyright 2014 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FEEDBACK_FEEDBACK_DAEMON_H_
#define FEEDBACK_FEEDBACK_DAEMON_H_

#include <memory>

#include "base/message_loop/message_loop.h"
#include "base/threading/sequenced_worker_pool.h"
#include "feedback/feedback_service.h"

#include <string>

namespace feedback {

class FeedbackUploader;

class Daemon final {
 public:
  // |url| specifies which url the reports will be sent to. Note that product
  // IDs may be unique to that server, so the clients also need to be
  // configured properly for the chosen server.
  explicit Daemon(const std::string& url);
  ~Daemon();

  // Does all the work. Blocks until the daemon is finished.
  void Run();

 private:
  base::MessageLoop loop_;
  scoped_refptr<base::SequencedWorkerPool> pool_;
  std::unique_ptr<feedback::FeedbackUploader> uploader_;

  DISALLOW_COPY_AND_ASSIGN(Daemon);
};

}  // namespace feedback

#endif  // FEEDBACK_FEEDBACK_DAEMON_H_
