// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_LIBSANE_WRAPPER_H_
#define LORGNETTE_LIBSANE_WRAPPER_H_

#include <sane/sane.h>

namespace lorgnette {

class LibsaneWrapper {
 public:
  LibsaneWrapper() = default;
  LibsaneWrapper(const LibsaneWrapper&) = delete;
  LibsaneWrapper& operator=(const LibsaneWrapper&) = delete;
  virtual ~LibsaneWrapper() = default;

  virtual SANE_Status sane_init(SANE_Int* version_code,
                                SANE_Auth_Callback authorize) = 0;
  virtual void sane_exit(void) = 0;
  virtual SANE_Status sane_get_devices(const SANE_Device*** device_list,
                                       SANE_Bool local_only) = 0;
  virtual SANE_Status sane_open(SANE_String_Const name, SANE_Handle* h) = 0;
  virtual void sane_close(SANE_Handle h) = 0;
  virtual const SANE_Option_Descriptor* sane_get_option_descriptor(
      SANE_Handle h, SANE_Int n) = 0;
  virtual SANE_Status sane_control_option(
      SANE_Handle h, SANE_Int n, SANE_Action a, void* v, SANE_Int* i) = 0;
  virtual SANE_Status sane_get_parameters(SANE_Handle h,
                                          SANE_Parameters* p) = 0;
  virtual SANE_Status sane_start(SANE_Handle h) = 0;
  virtual SANE_Status sane_read(SANE_Handle h,
                                SANE_Byte* buf,
                                SANE_Int maxlen,
                                SANE_Int* len) = 0;
  virtual void sane_cancel(SANE_Handle h) = 0;
  virtual SANE_Status sane_set_io_mode(SANE_Handle h, SANE_Bool m) = 0;
};

}  // namespace lorgnette

#endif  // LORGNETTE_LIBSANE_WRAPPER_H_
