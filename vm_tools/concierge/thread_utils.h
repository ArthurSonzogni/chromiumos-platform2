// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef VM_TOOLS_CONCIERGE_THREAD_UTILS_H_
#define VM_TOOLS_CONCIERGE_THREAD_UTILS_H_

#include <type_traits>
#include <utility>

#include "base/synchronization/waitable_event.h"
#include "base/task/task_runner.h"

namespace vm_tools::concierge {

// Runs |func| on the given |task_runner|. The calling task will block until
// |func| returns. Deadlocks if |task_runner| is sequenced with the calling
// task.
template <typename T>
T PostTaskAndWaitForResult(scoped_refptr<base::TaskRunner> task_runner,
                           base::OnceCallback<T()> func) {
  // Generate nicer compiler errors.
  static_assert(!std::is_same_v<T, void>,
                "Use \"PostTaskAndWait\" for tasks returning void");

  base::WaitableEvent event{};
  T result;

  task_runner->PostTask(
      FROM_HERE, base::BindOnce(
                     [](base::OnceCallback<T()> callback,
                        raw_ref<base::WaitableEvent> event, raw_ref<T> result) {
                       *result = std::move(callback).Run();
                       event->Signal();
                     },
                     std::move(func), raw_ref(event), raw_ref(result)));

  event.Wait();
  return result;
}

void PostTaskAndWait(scoped_refptr<base::TaskRunner> task_runner,
                     base::OnceCallback<void()> func);

}  // namespace vm_tools::concierge

#endif  // VM_TOOLS_CONCIERGE_THREAD_UTILS_H_
