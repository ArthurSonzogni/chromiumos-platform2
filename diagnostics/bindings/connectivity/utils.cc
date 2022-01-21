// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "diagnostics/bindings/connectivity/utils.h"

#include <utility>

#include <base/bind.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {
namespace {

void RunOrReturnCallback(
    bool return_value,
    base::OnceCallback<void(base::OnceCallback<void(bool)>)> run_callback,
    base::OnceCallback<void(bool)> return_callback,
    bool result) {
  if (result) {
    std::move(run_callback).Run(std::move(return_callback));
  } else {
    std::move(return_callback).Run(return_value);
  }
}
}  // namespace

void RunOrReturn(
    bool return_value,
    base::OnceCallback<void(base::OnceCallback<void(bool)>)> get_result,
    base::OnceCallback<void(base::OnceCallback<void(bool)>)> run_callback,
    base::OnceCallback<void(bool)> return_callback) {
  std::move(get_result)
      .Run(base::BindOnce(&RunOrReturnCallback, return_value,
                          std::move(run_callback), std::move(return_callback)));
}

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics
