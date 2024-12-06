// Copyright 2023 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "lorgnette/libsane_wrapper_fake.h"

#include <algorithm>

#include <base/check.h>
#include <base/notreached.h>

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
  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return nullptr;
  }
  FakeScanner& scanner = elem->second;
  if (n < 0 || n >= scanner.descriptors.size()) {
    return nullptr;
  }
  return &scanner.descriptors[n];
}

SANE_Status LibsaneWrapperFake::sane_control_option(
    SANE_Handle h, SANE_Int n, SANE_Action a, void* v, SANE_Int* i) {
  if (!v) {
    return SANE_STATUS_INVAL;
  }
  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return SANE_STATUS_UNSUPPORTED;
  }
  FakeScanner& scanner = elem->second;
  if (n < 0 || n >= scanner.descriptors.size() ||
      !scanner.values[n].has_value()) {
    return SANE_STATUS_UNSUPPORTED;
  }

  switch (a) {
    case SANE_ACTION_GET_VALUE:
      memcpy(v, scanner.values[n].value(), scanner.descriptors[n].size);
      return SANE_STATUS_GOOD;
    case SANE_ACTION_SET_VALUE:
      if (!(scanner.descriptors[n].cap & SANE_CAP_SOFT_SELECT)) {
        return SANE_STATUS_UNSUPPORTED;
      }
      memcpy(scanner.values[n].value(), v, scanner.descriptors[n].size);
      if (i) {
        *i = SANE_INFO_RELOAD_OPTIONS | SANE_INFO_RELOAD_PARAMS;
      }
      return SANE_STATUS_GOOD;
    default:
      return SANE_STATUS_UNSUPPORTED;
  }

  NOTREACHED();
}

SANE_Status LibsaneWrapperFake::sane_get_parameters(SANE_Handle h,
                                                    SANE_Parameters* p) {
  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return SANE_STATUS_INVAL;
  }
  FakeScanner& scanner = elem->second;
  if (scanner.parameters.has_value()) {
    *p = scanner.parameters.value();
    return SANE_STATUS_GOOD;
  } else {
    p = nullptr;
    return SANE_STATUS_IO_ERROR;
  }
}

SANE_Status LibsaneWrapperFake::sane_start(SANE_Handle h) {
  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return SANE_STATUS_INVAL;
  }
  FakeScanner& scanner = elem->second;
  return scanner.sane_start_result;
}

SANE_Status LibsaneWrapperFake::sane_read(SANE_Handle h,
                                          SANE_Byte* buf,
                                          SANE_Int maxlen,
                                          SANE_Int* len) {
  if (!buf || !len) {
    return SANE_STATUS_INVAL;
  }

  *len = 0;  // Required for any non-GOOD status.

  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return SANE_STATUS_INVAL;
  }
  FakeScanner& scanner = elem->second;

  if (scanner.current_read_response >= scanner.read_responses.size()) {
    return SANE_STATUS_IO_ERROR;
  }
  auto [status, want_len] =
      scanner.read_responses[scanner.current_read_response++];

  // Clamp to minimum available data and buffer space.
  SANE_Int to_read = std::min(maxlen, want_len);

  // Write `to_read` bytes of the current value.  The caller can verify each
  // sane_read() call by pairing up the counts in `buf`.
  for (SANE_Int i = 0; i < to_read; i++) {
    *buf++ = scanner.current_data_val;
  }
  ++scanner.current_data_val;  // Next read will return the next byte value.

  *len = to_read;
  return status;
}

void LibsaneWrapperFake::sane_cancel(SANE_Handle h) {}

SANE_Status LibsaneWrapperFake::sane_set_io_mode(SANE_Handle h, SANE_Bool m) {
  auto elem = scanners_.find(h);
  if (elem == scanners_.end()) {
    return SANE_STATUS_INVAL;
  }
  FakeScanner& scanner = elem->second;
  return scanner.supports_nonblocking ? SANE_STATUS_GOOD
                                      : SANE_STATUS_UNSUPPORTED;
}

SANE_Handle LibsaneWrapperFake::CreateScanner(const std::string& name) {
  static size_t scanner_id = 0;
  SANE_Handle h = reinterpret_cast<SANE_Handle>(++scanner_id);
  scanners_[h] = {
      .name = name,
      .handle = h,
      .sane_start_result = SANE_STATUS_IO_ERROR,
      .supports_nonblocking = true,  // Match the most popular backends.
      .current_data_val = 0,
      .current_read_response = 0,
  };
  return h;
}

void LibsaneWrapperFake::SetDescriptors(
    SANE_Handle handle,
    const std::vector<SANE_Option_Descriptor>& descriptors) {
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  scanner.descriptors = descriptors;
  scanner.values = std::vector<std::optional<void*>>(descriptors.size());
}

void LibsaneWrapperFake::SetParameters(
    SANE_Handle handle, const std::optional<SANE_Parameters>& parameters) {
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  scanner.parameters = parameters;
}

void LibsaneWrapperFake::AddSaneReadResponse(SANE_Handle handle,
                                             SANE_Status status,
                                             SANE_Int maxread) {
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  scanner.read_responses.emplace_back(status, maxread);
}

void LibsaneWrapperFake::SetOptionValue(SANE_Handle handle,
                                        size_t field,
                                        void* value) {
  CHECK(value);
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  CHECK(field < scanner.values.size());
  scanner.values[field] = value;
}

void LibsaneWrapperFake::SetSaneStartResult(SANE_Handle handle,
                                            SANE_Status result) {
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  scanner.sane_start_result = result;
}

void LibsaneWrapperFake::SetSupportsNonBlocking(SANE_Handle handle,
                                                bool support) {
  auto elem = scanners_.find(handle);
  CHECK(elem != scanners_.end());
  FakeScanner& scanner = elem->second;
  scanner.supports_nonblocking = support;
}

}  // namespace lorgnette
