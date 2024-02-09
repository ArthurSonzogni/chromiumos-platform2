// Copyright 2012 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UPDATE_ENGINE_COMMON_MOCK_PREFS_H_
#define UPDATE_ENGINE_COMMON_MOCK_PREFS_H_

#include <string>
#include <vector>

#include <gmock/gmock.h>

#include "update_engine/common/constants.h"
#include "update_engine/common/prefs_interface.h"

namespace chromeos_update_engine {

class MockPrefs : public PrefsInterface {
 public:
  MOCK_CONST_METHOD2(GetString,
                     bool(const std::string& key, std::string* value));
  MOCK_METHOD2(SetString,
               bool(const std::string& key, const std::string& value));
  MOCK_CONST_METHOD2(GetInt64, bool(const std::string& key, int64_t* value));
  MOCK_METHOD2(SetInt64, bool(const std::string& key, const int64_t value));

  MOCK_CONST_METHOD2(GetBoolean, bool(const std::string& key, bool* value));
  MOCK_METHOD2(SetBoolean, bool(const std::string& key, const bool value));

  MOCK_CONST_METHOD1(Exists, bool(const std::string& key));
  MOCK_METHOD1(Delete, bool(const std::string& key));
  MOCK_METHOD2(Delete,
               bool(const std::string& key,
                    const std::vector<std::string>& nss));

  MOCK_CONST_METHOD2(GetSubKeys,
                     bool(const std::string&, std::vector<std::string>*));

  MOCK_METHOD2(AddObserver, void(const std::string& key, ObserverInterface*));
  MOCK_METHOD2(RemoveObserver,
               void(const std::string& key, ObserverInterface*));
};

}  // namespace chromeos_update_engine

#endif  // UPDATE_ENGINE_COMMON_MOCK_PREFS_H_
