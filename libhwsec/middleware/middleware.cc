// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "libhwsec/middleware/middleware.h"

#include <libhwsec-foundation/tpm/tpm_version.h>

#include "libhwsec/backend/backend.h"
#include "libhwsec/proxy/proxy_impl.h"
#include "libhwsec/status.h"

#if USE_TPM2
#include "libhwsec/backend/tpm2/backend.h"
#endif

#if USE_TPM1
#include "libhwsec/backend/tpm1/backend.h"
#endif

namespace {
constexpr char kThreadName[] = "libhwsec_thread";
}  // namespace

namespace hwsec {

MiddlewareOwner::MiddlewareOwner() {
  background_thread_ = std::make_unique<base::Thread>(kThreadName);
  background_thread_->StartWithOptions(
      base::Thread::Options(base::MessagePumpType::IO, 0));
  task_runner_ = background_thread_->task_runner();
  thread_id_ = background_thread_->GetThreadId();
  base::OnceClosure task = base::BindOnce(&MiddlewareOwner::InitBackend,
                                          weak_factory_.GetWeakPtr(), nullptr);
  task_runner_->PostTask(FROM_HERE, std::move(task));
}

MiddlewareOwner::MiddlewareOwner(scoped_refptr<base::TaskRunner> task_runner,
                                 base::PlatformThreadId thread_id) {
  CHECK(task_runner || thread_id == base::PlatformThread::CurrentId());
  task_runner_ = std::move(task_runner);
  base::OnceClosure task = base::BindOnce(&MiddlewareOwner::InitBackend,
                                          weak_factory_.GetWeakPtr(), nullptr);
  thread_id_ = thread_id;
  if (thread_id == base::PlatformThread::CurrentId()) {
    std::move(task).Run();
  } else {
    task_runner_->PostTask(FROM_HERE, std::move(task));
  }
}

MiddlewareOwner::MiddlewareOwner(std::unique_ptr<Backend> custom_backend,
                                 scoped_refptr<base::TaskRunner> task_runner,
                                 base::PlatformThreadId thread_id) {
  CHECK(task_runner || thread_id == base::PlatformThread::CurrentId());
  task_runner_ = std::move(task_runner);
  base::OnceClosure task =
      base::BindOnce(&MiddlewareOwner::InitBackend, weak_factory_.GetWeakPtr(),
                     std::move(custom_backend));
  thread_id_ = thread_id;
  if (thread_id == base::PlatformThread::CurrentId()) {
    std::move(task).Run();
  } else {
    task_runner_->PostTask(FROM_HERE, std::move(task));
  }
}

MiddlewareOwner::~MiddlewareOwner() {
  // Post blocking task if we the backend thread had been initialized.
  if (thread_id_ != base::kInvalidThreadId) {
    base::OnceClosure task = base::BindOnce(&MiddlewareOwner::FiniBackend,
                                            weak_factory_.GetWeakPtr());
    Middleware(Derive()).RunBlockingTask(std::move(task));
  }
}

MiddlewareDerivative MiddlewareOwner::Derive() {
  CHECK(thread_id_ != base::kInvalidThreadId);
  return MiddlewareDerivative{
      .task_runner = task_runner_,
      .thread_id = thread_id_,
      .middleware = weak_factory_.GetWeakPtr(),
  };
}

void MiddlewareOwner::InitBackend(std::unique_ptr<Backend> custom_backend) {
  CHECK(!backend_) << "Should not init backend twice.";

  if (thread_id_ == base::kInvalidThreadId) {
    thread_id_ = base::PlatformThread::CurrentId();
  }

  if (custom_backend != nullptr) {
    backend_ = std::move(custom_backend);
    return;
  }

  TPM_SELECT_BEGIN;
  TPM1_SECTION({
    auto proxy = std::make_unique<ProxyImpl>();
    if (!proxy->Init()) {
      LOG(ERROR) << "Failed to init hwsec proxy";
      return;
    }
    proxy_ = std::move(proxy);
    backend_ = std::make_unique<BackendTpm1>(*proxy_, Derive());
  });
  TPM2_SECTION({
    auto proxy = std::make_unique<ProxyImpl>();
    if (!proxy->Init()) {
      LOG(ERROR) << "Failed to init hwsec proxy";
      return;
    }
    proxy_ = std::move(proxy);
    backend_ = std::make_unique<BackendTpm2>(*proxy_, Derive());
  });
  OTHER_TPM_SECTION({
    LOG(ERROR) << "Calling on unsupported TPM platform.";
    return;
  });
  TPM_SELECT_END;
}

void MiddlewareOwner::FiniBackend() {
  backend_.reset();
  proxy_.reset();
}

}  // namespace hwsec
