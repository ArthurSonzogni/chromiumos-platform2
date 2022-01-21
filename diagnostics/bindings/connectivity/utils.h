// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef DIAGNOSTICS_BINDINGS_CONNECTIVITY_UTILS_H_
#define DIAGNOSTICS_BINDINGS_CONNECTIVITY_UTILS_H_

#include <base/callback.h>

namespace diagnostics {
namespace bindings {
namespace connectivity {

// Runs or returns. Gets the result of the callback of |get_result|. If the
// result is true, runs the |run_callback| and passes the |return_callback| as
// argument. If false, runs the |return_callback| with the |return_value|.
// The blocking version of this is:
//   if (!get_result())
//      return return_value;
//   // keep running.
void RunOrReturn(
    bool return_value,
    base::OnceCallback<void(base::OnceCallback<void(bool)>)> get_result,
    base::OnceCallback<void(base::OnceCallback<void(bool)>)> run_callback,
    base::OnceCallback<void(bool)> return_callback);

}  // namespace connectivity
}  // namespace bindings
}  // namespace diagnostics

#endif  // DIAGNOSTICS_BINDINGS_CONNECTIVITY_UTILS_H_
