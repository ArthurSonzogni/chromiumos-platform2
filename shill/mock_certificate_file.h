// Copyright (c) 2013 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SHILL_MOCK_CERTFICATE_FILE_H_
#define SHILL_MOCK_CERTFICATE_FILE_H_

#include <gmock/gmock.h>

#include "shill/certificate_file.h"

namespace shill {

class MockCertificateFile : public CertificateFile {
 public:
  MockCertificateFile();
  virtual ~MockCertificateFile();

  MOCK_METHOD1(CreatePEMFromString,
               base::FilePath(const std::string &pem_contents));
  MOCK_METHOD1(CreatePEMFromStrings,
               base::FilePath(const std::vector<std::string> &pem_contents));

 private:
  DISALLOW_COPY_AND_ASSIGN(MockCertificateFile);
};

}  // namespace shill

#endif  // SHILL_MOCK_CERTIFICATE_FILE_H_
