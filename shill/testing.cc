// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/testing.h"

#include <utility>

#include <base/bind.h>
#include <base/callback.h>
#include <base/check.h>
#include <base/run_loop.h>

namespace shill {

void SetErrorAndReturn(base::RepeatingClosure quit_closure,
                       Error* to_return,
                       const Error& error) {
  to_return->CopyFrom(error);
  quit_closure.Run();
}

void SetEnabledSync(Device* device, bool enable, bool persist, Error* error) {
  CHECK(device);
  CHECK(error);

  base::RunLoop run_loop;
  device->SetEnabledChecked(
      enable, persist,
      base::BindRepeating(&SetErrorAndReturn, run_loop.QuitClosure(), error));
  run_loop.Run();
}

template <>
void ReturnOperationFailed<ResultCallback>(const ResultCallback& callback) {
  callback.Run(Error(Error::kOperationFailed));
}

template <>
void ReturnOperationFailed<RpcIdentifierCallback>(
    RpcIdentifierCallback callback) {
  std::move(callback).Run(RpcIdentifier(""), Error(Error::kOperationFailed));
}

template <>
void ReturnOperationFailed<StringCallback>(const StringCallback& callback) {
  callback.Run("", Error(Error::kOperationFailed));
}

}  // namespace shill
