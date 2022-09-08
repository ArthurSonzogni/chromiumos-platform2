/*
 * Copyright 2022 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "gpu/gpu_resources.h"

#include <iomanip>

#include "base/bind.h"
#include "base/sequence_checker.h"
#include "cros-camera/future.h"
#include "gpu/tracing.h"

namespace cros {

GpuResources::GpuResources() : gpu_thread_("GpuResourcesThread") {
  CHECK(gpu_thread_.Start());
}

GpuResources::~GpuResources() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(gpu_thread_sequence);
  gpu_thread_.Stop();
}

bool GpuResources::Initialize() {
  auto future = Future<bool>::Create(nullptr);
  PostGpuTaskSync(
      FROM_HERE,
      base::BindOnce(&GpuResources::InitializeOnGpuThread,
                     base::Unretained((this)), GetFutureCallback(future)));
  return future->Wait();
}

void GpuResources::InitializeOnGpuThread(base::OnceCallback<void(bool)> cb) {
  DCHECK(gpu_thread_.IsCurrentThread());
  TRACE_GPU();

  if (!egl_context_) {
    egl_context_ = EglContext::GetSurfacelessContext();
    if (!egl_context_->IsValid()) {
      LOGF(ERROR) << "Failed to create EGL context";
      std::move(cb).Run(false);
      return;
    }
  }
  if (!egl_context_->MakeCurrent()) {
    LOGF(ERROR) << "Failed to make EGL context current";
    std::move(cb).Run(false);
    return;
  }

  image_processor_ = std::make_unique<GpuImageProcessor>();
  if (!image_processor_) {
    LOGF(ERROR) << "Failed to create GpuImageProcessor";
    std::move(cb).Run(false);
    return;
  }

  std::move(cb).Run(true);
}

GpuResources::CacheContainer* GpuResources::GetCache(const std::string id) {
  DCHECK(gpu_thread_.IsCurrentThread());
  TRACE_GPU();

  if (cache_.count(id) == 1) {
    return cache_.at(id).get();
  }
  return nullptr;
}

void GpuResources::SetCache(
    const std::string id,
    std::unique_ptr<GpuResources::CacheContainer> container) {
  DCHECK(gpu_thread_.IsCurrentThread());
  TRACE_GPU();

  CHECK_EQ(0, cache_.count(id));
  cache_.emplace(id, std::move(container));
}

void GpuResources::ClearCache(const std::string id) {
  DCHECK(gpu_thread_.IsCurrentThread());
  TRACE_GPU();

  if (cache_.erase(id) == 0) {
    VLOGF(1) << "Cache entry for " << std::quoted(id) << " does not exist";
  }
}

}  // namespace cros
