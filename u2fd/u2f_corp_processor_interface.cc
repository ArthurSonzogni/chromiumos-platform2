// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_corp_processor_interface.h"

#include <dlfcn.h>

#include <base/logging.h>

#include "u2fd/client/u2f_corp_processor.h"

namespace u2f {

typedef U2fCorpProcessor* create_t();
typedef void destroy_t(U2fCorpProcessor*);

U2fCorpProcessorInterface::U2fCorpProcessorInterface() {
  handle_ = dlopen("libu2fd-corp.so", RTLD_LAZY);
  if (!handle_) {
    LOG(WARNING) << "Cannot load library: " << dlerror();
    return;
  }
  // Reset errors.
  dlerror();

  create_t* create_processor =
      reinterpret_cast<create_t*>(dlsym(handle_, "create"));
  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    LOG(FATAL) << "Cannot load symbol create: " << dlsym_error;
    return;
  }

  processor_ = create_processor();
}

U2fCorpProcessorInterface::~U2fCorpProcessorInterface() {
  if (!handle_) {
    return;
  }
  destroy_t* destroy_processor =
      reinterpret_cast<destroy_t*>(dlsym(handle_, "destroy"));
  const char* dlsym_error = dlerror();
  if (dlsym_error) {
    LOG(FATAL) << "Cannot load symbol destroy: " << dlsym_error;
    return;
  }

  destroy_processor(processor_);
  dlclose(handle_);
}

void U2fCorpProcessorInterface::Initialize() {
  if (processor_) {
    processor_->Initialize();
  } else {
    LOG(INFO) << "Stub initialized.";
  }
}

}  // namespace u2f
