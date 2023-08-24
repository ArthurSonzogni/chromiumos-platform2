// Copyright 2018 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "runtime_probe/functions/sequence.h"

#include <utility>

#include <base/functional/bind.h>
#include <base/functional/callback.h>
#include <base/logging.h>
#include <base/values.h>

namespace runtime_probe {

void SequenceFunction::RunNext(base::OnceCallback<void(DataType)> callback,
                               int idx,
                               base::Value::Dict result_dict,
                               SequenceFunction::DataType probe_result) const {
  if (probe_result.size() == 0) {
    std::move(callback).Run({});
    return;
  }
  if (probe_result.size() > 1) {
    LOG(ERROR) << "Subfunction call generates more than one results.";
    std::move(callback).Run({});
    return;
  }
  result_dict.Merge(probe_result[0].GetDict().Clone());

  if (idx >= functions_.size()) {
    DataType results;
    results.Append(std::move(result_dict));
    std::move(callback).Run(std::move(results));
    return;
  }
  functions_[idx]->Eval(
      base::BindOnce(&SequenceFunction::RunNext, base::Unretained(this),
                     std::move(callback), idx + 1, std::move(result_dict)));
}

void SequenceFunction::EvalAsyncImpl(
    base::OnceCallback<void(DataType)> callback) const {
  if (functions_.size() == 0) {
    std::move(callback).Run({});
  }
  callback = base::BindOnce(&SequenceFunction::RunNext, base::Unretained(this),
                            std::move(callback), 1, base::Value::Dict{});
  functions_[0]->Eval(std::move(callback));
}

}  // namespace runtime_probe
