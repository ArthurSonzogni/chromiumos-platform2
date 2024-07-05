// Copyright 2024 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ODML_ON_DEVICE_MODEL_ML_SESSION_ACCESSOR_H_
#define ODML_ON_DEVICE_MODEL_ML_SESSION_ACCESSOR_H_

#include <memory>
#include <string>

#include <base/files/file.h>
#include <base/memory/scoped_refptr.h>
#include <base/task/sequenced_task_runner.h>

#include "odml/mojom/on_device_model.mojom.h"
#include "odml/on_device_model/ml/chrome_ml_api.h"

namespace ml {

// Allows for safely accessing ChromeMLSession on a task runner. ChromeMLSession
// may make blocking calls, so it can't be used on the main thread.
class SessionAccessor {
 public:
  using Ptr = std::unique_ptr<SessionAccessor, base::OnTaskRunnerDeleter>;

  static Ptr Empty();
  static Ptr Create(scoped_refptr<base::SequencedTaskRunner> task_runner,
                    ChromeMLModel model,
                    base::File adaptation_data = base::File());

  ~SessionAccessor();

  // These methods forward to the relevant ChromeMLSession methods on the task
  // runner.
  Ptr Clone();
  ChromeMLCancelFn Execute(on_device_model::mojom::InputOptionsPtr input,
                           ChromeMLExecutionOutputFn output_fn,
                           ChromeMLContextSavedFn context_saved_fn);
  void Score(const std::string& text, ChromeMLScoreFn score_fn);
  void SizeInTokens(const std::string& text,
                    ChromeMLSizeInTokensFn size_in_tokens_fn);

 private:
  class Canceler;

  SessionAccessor(scoped_refptr<base::SequencedTaskRunner> task_runner,
                  ChromeMLModel model);

  void CloneFrom(SessionAccessor* other);
  void CreateInternal(base::File adaptation_data);
  void ExecuteInternal(on_device_model::mojom::InputOptionsPtr input,
                       ChromeMLExecutionOutputFn output_fn,
                       ChromeMLContextSavedFn context_saved_fn,
                       scoped_refptr<Canceler> canceler);
  void ScoreInternal(const std::string& text, ChromeMLScoreFn score_fn);
  void SizeInTokensInternal(const std::string& text,
                            ChromeMLSizeInTokensFn size_in_tokens_fn);

  scoped_refptr<base::SequencedTaskRunner> task_runner_;
  ChromeMLModel model_;
  ChromeMLSession session_ = 0;
};

}  // namespace ml

#endif  // ODML_ON_DEVICE_MODEL_ML_SESSION_ACCESSOR_H_
