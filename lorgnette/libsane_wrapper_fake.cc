// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/libsane_wrapper_fake.h"

namespace lorgnette {

std::unique_ptr<LibsaneWrapper> LibsaneWrapperFake::Create() {
  return std::unique_ptr<LibsaneWrapper>(new LibsaneWrapperFake());
}

SANE_Status LibsaneWrapperFake::sane_init(SANE_Int* version_code,
                                          SANE_Auth_Callback authorize) {
  return SANE_STATUS_GOOD;
}

void LibsaneWrapperFake::sane_exit(void) {}

SANE_Status LibsaneWrapperFake::sane_get_devices(
    const SANE_Device*** device_list, SANE_Bool local_only) {
  return SANE_STATUS_IO_ERROR;
}

SANE_Status LibsaneWrapperFake::sane_open(SANE_String_Const name,
                                          SANE_Handle* h) {
  for (const auto& kv : scanners_) {
    if (kv.second.name == name) {
      *h = kv.first;
      return SANE_STATUS_GOOD;
    }
  }

  return SANE_STATUS_INVAL;
}

void LibsaneWrapperFake::sane_close(SANE_Handle h) {
  scanners_.erase(h);
}

const SANE_Option_Descriptor* LibsaneWrapperFake::sane_get_option_descriptor(
    SANE_Handle h, SANE_Int n) {
  return nullptr;
}

SANE_Status LibsaneWrapperFake::sane_control_option(
    SANE_Handle h, SANE_Int n, SANE_Action a, void* v, SANE_Int* i) {
  return SANE_STATUS_IO_ERROR;
}

SANE_Status LibsaneWrapperFake::sane_get_parameters(SANE_Handle h,
                                                    SANE_Parameters* p) {
  return SANE_STATUS_IO_ERROR;
}

SANE_Status LibsaneWrapperFake::sane_start(SANE_Handle h) {
  return SANE_STATUS_IO_ERROR;
}

SANE_Status LibsaneWrapperFake::sane_read(SANE_Handle h,
                                          SANE_Byte* buf,
                                          SANE_Int maxlen,
                                          SANE_Int* len) {
  return SANE_STATUS_IO_ERROR;
}

void LibsaneWrapperFake::sane_cancel(SANE_Handle h) {}

SANE_Handle LibsaneWrapperFake::CreateScanner(const std::string& name) {
  static size_t scanner_id = 0;
  SANE_Handle h = reinterpret_cast<SANE_Handle>(++scanner_id);
  scanners_[h] = {
      name,  // name
      h,     // handle
  };
  return h;
}

}  // namespace lorgnette
