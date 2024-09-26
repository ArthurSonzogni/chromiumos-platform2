// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "shill/testing.h"

#include <map>
#include <string_view>
#include <utility>

#include <base/check.h>
#include <base/files/file_path.h>
#include <base/files/file_util.h>
#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/run_loop.h>

namespace shill {

base::OnceCallback<void(const Error&)> GetResultCallback(
    base::test::TestFuture<Error>* e) {
  return base::BindOnce([](base::OnceCallback<void(Error)> future_cb,
                           const Error& e) { std::move(future_cb).Run(e); },
                        e->GetCallback());
}

void SetEnabledSync(Device* device, bool enable, bool persist, Error* error) {
  CHECK(device);
  CHECK(error);

  base::test::TestFuture<Error> e;
  device->SetEnabledChecked(enable, persist, GetResultCallback(&e));
  *error = e.Get();
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

template <>
void ReturnOperationFailed<BrilloAnyCallback>(BrilloAnyCallback callback) {
  std::move(callback).Run(std::map<uint32_t, brillo::Any>(),
                          Error(Error::kOperationFailed));
}

base::FilePath GetFilePathForTest(std::string_view filename) {
  const char* out_dir = getenv("OUT");
  if (out_dir != nullptr) {
    return base::FilePath(out_dir).Append(filename);
  }

  char exec_path[PATH_MAX];
  PCHECK(realpath("/proc/self/exe", exec_path));
  return base::FilePath(exec_path).DirName().Append(filename);
}

}  // namespace shill
