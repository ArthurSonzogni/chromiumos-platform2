// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/testing.h"

#include <utility>

#include <base/check.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/run_loop.h>

namespace shill {

void SetErrorAndReturn(base::RepeatingClosure quit_closure,
                       Error* to_return,
                       const Error& error) {
  *to_return = error;
  quit_closure.Run();
}

base::OnceCallback<void(const Error&)> GetResultCallback(
    base::test::TestFuture<Error>* e) {
  return base::BindOnce([](base::OnceCallback<void(Error)> future_cb,
                           const Error& e) { std::move(future_cb).Run(e); },
                        e->GetCallback());
}

void SetEnabledSync(Device* device, bool enable, bool persist, Error* error) {
  CHECK(device);
  CHECK(error);

  base::RunLoop run_loop;
  device->SetEnabledChecked(
      enable, persist,
      base::BindOnce(&SetErrorAndReturn, run_loop.QuitClosure(), error));
  run_loop.Run();
}

template <>
void ReturnOperationFailed<ResultCallback>(ResultCallback callback) {
  std::move(callback).Run(Error(Error::kOperationFailed));
}

template <>
void ReturnOperationFailed<RpcIdentifierCallback>(
    RpcIdentifierCallback callback) {
  std::move(callback).Run(RpcIdentifier(""), Error(Error::kOperationFailed));
}

template <>
void ReturnOperationFailed<StringCallback>(StringCallback callback) {
  std::move(callback).Run("", Error(Error::kOperationFailed));
}

}  // namespace shill
