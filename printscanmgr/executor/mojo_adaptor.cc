// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "printscanmgr/executor/mojo_adaptor.h"

#include <string>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <brillo/files/safe_fd.h>

namespace printscanmgr {

namespace {

constexpr char kPpdDirectory[] = "/var/cache/cups/printers/ppd";

}  // namespace

MojoAdaptor::MojoAdaptor(
    const scoped_refptr<base::SingleThreadTaskRunner> mojo_task_runner,
    mojo::PendingReceiver<mojom::Executor> receiver,
    base::OnceClosure on_disconnect)
    : mojo_task_runner_(mojo_task_runner),
      receiver_{/*impl=*/this, std::move(receiver)} {
  receiver_.set_disconnect_handler(std::move(on_disconnect));

  auto bus = connection_.Connect();
  CHECK(bus) << "Failed to connect to the D-Bus system bus.";
  upstart_tools_ = UpstartTools::Create(bus);
}

MojoAdaptor::~MojoAdaptor() = default;

void MojoAdaptor::RestartUpstartJob(mojom::UpstartJob job,
                                    RestartUpstartJobCallback callback) {
  std::string error;
  bool success = upstart_tools_->RestartJob(job, &error);
  std::move(callback).Run(success, error);
}

void MojoAdaptor::GetPpdFile(const std::string& file_name,
                             GetPpdFileCallback callback) {
  // Get just the filename from the input and build a new path with the known
  // cups PPD directory.  Doing it this way for security reasons - making sure
  // we use a known good directory and not trusting the input from printscanmgr.
  const base::FilePath ppdPath =
      base::FilePath(kPpdDirectory).Append(file_name);

  // Use SafeFD to read the file - more secure than just using file utils.
  auto [ppdFd, err1] = brillo::SafeFD::Root().first.OpenExistingFile(
      ppdPath, O_RDONLY | O_CLOEXEC);
  if (brillo::SafeFD::IsError(err1)) {
    LOG(ERROR) << "Unable to open " << ppdPath << ": "
               << static_cast<int>(err1);
    std::move(callback).Run(/*file_contents=*/"", /*success=*/false);
    return;
  }

  auto [contents, err2] = ppdFd.ReadContents();
  if (brillo::SafeFD::IsError(err2)) {
    LOG(ERROR) << "Unable to read contents of " << ppdPath << ": "
               << static_cast<int>(err2);
    std::move(callback).Run(/*file_contents=*/"", /*success=*/false);
    return;
  }

  std::move(callback).Run(std::string(contents.begin(), contents.end()),
                          /*success=*/true);
}

}  // namespace printscanmgr
