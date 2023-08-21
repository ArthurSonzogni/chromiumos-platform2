// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/libsane_wrapper_impl.h"

namespace lorgnette {

std::unique_ptr<LibsaneWrapper> LibsaneWrapperImpl::Create() {
  return std::unique_ptr<LibsaneWrapper>(new LibsaneWrapperImpl());
}

SANE_Status LibsaneWrapperImpl::sane_init(SANE_Int* version_code,
                                          SANE_Auth_Callback authorize) {
  return ::sane_init(version_code, authorize);
}

void LibsaneWrapperImpl::sane_exit(void) {
  ::sane_exit();
}

SANE_Status LibsaneWrapperImpl::sane_get_devices(
    const SANE_Device*** device_list, SANE_Bool local_only) {
  return ::sane_get_devices(device_list, local_only);
}

SANE_Status LibsaneWrapperImpl::sane_open(SANE_String_Const name,
                                          SANE_Handle* h) {
  return ::sane_open(name, h);
}

void LibsaneWrapperImpl::sane_close(SANE_Handle h) {
  ::sane_close(h);
}

const SANE_Option_Descriptor* LibsaneWrapperImpl::sane_get_option_descriptor(
    SANE_Handle h, SANE_Int n) {
  return ::sane_get_option_descriptor(h, n);
}

SANE_Status LibsaneWrapperImpl::sane_control_option(
    SANE_Handle h, SANE_Int n, SANE_Action a, void* v, SANE_Int* i) {
  return ::sane_control_option(h, n, a, v, i);
}

SANE_Status LibsaneWrapperImpl::sane_get_parameters(SANE_Handle h,
                                                    SANE_Parameters* p) {
  return ::sane_get_parameters(h, p);
}

SANE_Status LibsaneWrapperImpl::sane_start(SANE_Handle h) {
  return ::sane_start(h);
}

SANE_Status LibsaneWrapperImpl::sane_read(SANE_Handle h,
                                          SANE_Byte* buf,
                                          SANE_Int maxlen,
                                          SANE_Int* len) {
  return ::sane_read(h, buf, maxlen, len);
}

void LibsaneWrapperImpl::sane_cancel(SANE_Handle h) {
  ::sane_cancel(h);
}

}  // namespace lorgnette
