// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef FACED_FACE_SERVICE_H_
#define FACED_FACE_SERVICE_H_

#include <memory>
#include <utility>

#include <absl/status/status.h>
#include <absl/status/statusor.h>
#include <base/files/scoped_file.h>
#include <base/threading/sequenced_task_runner_handle.h>
#include <libminijail.h>
#include <scoped_minijail.h>

namespace faced {

// FaceServiceProcess contains the minijail process and file descriptor of the
// gRPC service application.
class FaceServiceProcess {
 public:
  static absl::StatusOr<std::unique_ptr<FaceServiceProcess>> Create();

  FaceServiceProcess() = default;
  ~FaceServiceProcess() = default;

  // Disallow copy and move.
  FaceServiceProcess(const FaceServiceProcess&) = delete;
  FaceServiceProcess& operator=(const FaceServiceProcess&) = delete;

  // Starts the process.
  absl::Status Start();

  // Stops the process
  absl::Status Shutdown();

 private:
  // The Minijail containing the launched FaceService app.
  ScopedMinijail jail_;

  // The socket connection to the FaceService app.
  base::ScopedFD fd_;
};

// FaceServiceManager contains the FaceServiceProcess and is responsible for
// leasing out an exclusive client.
class FaceServiceManager {
 public:
  static std::unique_ptr<FaceServiceManager> Create();

  FaceServiceManager() = default;
  ~FaceServiceManager() = default;

  // Disallow copy and move.
  FaceServiceManager(const FaceServiceManager&) = delete;
  FaceServiceManager& operator=(const FaceServiceManager&) = delete;

 private:
  std::unique_ptr<FaceServiceProcess> process_;
};

}  // namespace faced

#endif  // FACED_FACE_SERVICE_H_
