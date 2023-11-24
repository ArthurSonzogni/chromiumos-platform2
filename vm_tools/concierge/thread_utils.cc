// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <utility>

#include "vm_tools/concierge/thread_utils.h"

namespace vm_tools::concierge {

void PostTaskAndWait(scoped_refptr<base::TaskRunner> task_runner,
                     base::OnceCallback<void()> func) {
  base::WaitableEvent event{};

  task_runner->PostTask(FROM_HERE, base::BindOnce(
                                       [](base::OnceCallback<void()> callback,
                                          raw_ref<base::WaitableEvent> event) {
                                         std::move(callback).Run();
                                         event->Signal();
                                       },
                                       std::move(func), raw_ref(event)));

  event.Wait();
}

}  // namespace vm_tools::concierge
