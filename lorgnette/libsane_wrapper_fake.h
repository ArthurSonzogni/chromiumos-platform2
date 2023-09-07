// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef LORGNETTE_LIBSANE_WRAPPER_FAKE_H_
#define LORGNETTE_LIBSANE_WRAPPER_FAKE_H_

#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "lorgnette/libsane_wrapper.h"

namespace lorgnette {

class LibsaneWrapperFake : public LibsaneWrapper {
 public:
  LibsaneWrapperFake() = default;
  LibsaneWrapperFake(const LibsaneWrapperFake&) = delete;
  LibsaneWrapperFake& operator=(const LibsaneWrapperFake&) = delete;
  ~LibsaneWrapperFake() override = default;

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
  SANE_Status sane_set_io_mode(SANE_Handle h, SANE_Bool m) override;

  // Creates a handle that will be returned by sane_open(`name`).
  SANE_Handle CreateScanner(const std::string& name);

  // Sets descriptors that will be returned by sane_get_option_descriptor().
  void SetDescriptors(SANE_Handle handle,
                      const std::vector<SANE_Option_Descriptor>& descriptors);

  // Assigns memory for option `field` on `handle`.  The memory pointed to by
  // `value` must be large enough to contain whatever the option type demands
  // and must remain valid until this object is destroyed.  The contents of
  // `value` will be set and retrieved through sane_control_option().
  void SetOptionValue(SANE_Handle handle, size_t field, void* value);

  void SetSaneStartResult(SANE_Handle handle, SANE_Status result);
  void SetSupportsNonBlocking(SANE_Handle handle, bool support);

 protected:
  struct FakeScanner {
    std::string name;
    SANE_Handle handle;
    std::vector<SANE_Option_Descriptor> descriptors;
    std::vector<std::optional<void*>> values;
    SANE_Status sane_start_result;
    bool supports_nonblocking;
  };

  std::unordered_map<SANE_Handle, FakeScanner> scanners_;
};

}  // namespace lorgnette

#endif  // LORGNETTE_LIBSANE_WRAPPER_FAKE_H_
