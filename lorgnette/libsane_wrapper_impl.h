// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_LIBSANE_WRAPPER_IMPL_H_
#define LORGNETTE_LIBSANE_WRAPPER_IMPL_H_

#include <memory>

#include "lorgnette/libsane_wrapper.h"

namespace lorgnette {

class LibsaneWrapperImpl : public LibsaneWrapper {
 public:
  LibsaneWrapperImpl() = default;
  LibsaneWrapperImpl(const LibsaneWrapperImpl&) = delete;
  LibsaneWrapperImpl& operator=(const LibsaneWrapperImpl&) = delete;
  ~LibsaneWrapperImpl() override = default;

  static std::unique_ptr<LibsaneWrapper> Create();

  SANE_Status sane_init(SANE_Int* version_code,
                        SANE_Auth_Callback authorize) override;
  void sane_exit(void) override;
  SANE_Status sane_get_devices(const SANE_Device*** device_list,
                               SANE_Bool local_only) override;
  SANE_Status sane_open(SANE_String_Const name, SANE_Handle* h) override;
  void sane_close(SANE_Handle h) override;
  const SANE_Option_Descriptor* sane_get_option_descriptor(SANE_Handle h,
                                                           SANE_Int n) override;
  SANE_Status sane_control_option(
      SANE_Handle h, SANE_Int n, SANE_Action a, void* v, SANE_Int* i) override;
  SANE_Status sane_get_parameters(SANE_Handle h, SANE_Parameters* p) override;
  SANE_Status sane_start(SANE_Handle h) override;
  SANE_Status sane_read(SANE_Handle h,
                        SANE_Byte* buf,
                        SANE_Int maxlen,
                        SANE_Int* len) override;
  void sane_cancel(SANE_Handle h) override;
};

}  // namespace lorgnette

#endif  // LORGNETTE_LIBSANE_WRAPPER_IMPL_H_
