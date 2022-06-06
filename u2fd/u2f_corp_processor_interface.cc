// Copyright 2022 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "u2fd/u2f_corp_processor_interface.h"

#include <dlfcn.h>

#include <functional>

#include <base/logging.h>
#include <trunks/cr50_headers/u2f.h>

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

void U2fCorpProcessorInterface::Initialize(
    org::chromium::SessionManagerInterfaceProxy* sm_proxy,
    TpmVendorCommandProxy* tpm_proxy,
    std::function<void()> request_presence) {
  if (processor_) {
    processor_->Initialize(sm_proxy, tpm_proxy, request_presence);
  } else {
    VLOG(1) << "Stub initialized.";
  }
}

U2fResponseApdu U2fCorpProcessorInterface::ProcessApdu(
    const U2fCommandApdu& apdu) {
  if (processor_) {
    return processor_->ProcessApdu(apdu);
  }
  VLOG(1) << "Stub received ProcessApdu, doing nothing.";
  U2fResponseApdu resp_apdu;
  resp_apdu.SetStatus(U2F_SW_INS_NOT_SUPPORTED);
  return resp_apdu;
}

}  // namespace u2f
