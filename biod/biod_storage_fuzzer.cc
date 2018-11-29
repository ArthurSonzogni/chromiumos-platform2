// Copyright 2018 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "biod/biod_storage.h"

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include <base/bind.h>
#include <base/files/file_path.h>
#include <base/values.h>

#include "base/logging.h"
#include "base/test/fuzzed_data_provider.h"

class Environment {
 public:
  Environment() { logging::SetMinLogLevel(logging::LOG_FATAL); }
};

class TestRecord : public biod::BiometricsManager::Record {
 public:
  TestRecord(const std::string& id,
             const std::string& user_id,
             const std::string& label,
             const std::string& data)
      : id_(id), user_id_(user_id), label_(label), data_(data) {}

  const std::string& GetId() const override { return id_; }
  const std::string& GetUserId() const override { return user_id_; }
  const std::string& GetLabel() const override { return label_; }
  const std::string& GetData() const { return data_; }

  bool SetLabel(std::string label) override { return true; }
  bool Remove() override { return true; }

 private:
  std::string id_;
  std::string user_id_;
  std::string label_;
  std::string data_;
};

static std::vector<TestRecord> records;

static bool LoadRecord(const std::string& user_id,
                       const std::string& label,
                       const std::string& record_id,
                       const base::Value& data_value) {
  std::string data;
  data_value.GetAsString(&data);
  records.push_back(TestRecord(record_id, user_id, label, data));
  return true;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
  static Environment env;
  int MAX_LEN = 255;
  int MAX_DATA_LEN = 45000;

  base::FuzzedDataProvider data_provider(data, size);

  int id_len = data_provider.ConsumeInt32InRange(1, MAX_LEN);
  int user_id_len = data_provider.ConsumeInt32InRange(1, MAX_LEN);
  int label_len = data_provider.ConsumeInt32InRange(1, MAX_LEN);
  int data_len = data_provider.ConsumeInt32InRange(MAX_DATA_LEN - 1000,
                                                   MAX_DATA_LEN);

  const std::string id = data_provider.ConsumeBytes(id_len);
  const std::string user_id = data_provider.ConsumeBytes(user_id_len);
  const std::string label = data_provider.ConsumeBytes(label_len);
  std::string biod_data;

  if (data_provider.remaining_bytes() > data_len)
    biod_data = data_provider.ConsumeBytes(data_len);
  else
    biod_data = data_provider.ConsumeRemainingBytes();

  biod::BiodStorage biod_storage =
        biod::BiodStorage("BiometricsManager", base::Bind(&LoadRecord));
  biod_storage.set_allow_access(true);

  std::unique_ptr<TestRecord> record
    (new TestRecord(id, user_id, label, (const std::string) biod_data));

  base::FilePath root_path("/tmp/biod_storage_fuzzing_data");
  biod_storage.SetRootPathForTesting(root_path);
  bool status =
    biod_storage.WriteRecord((const TestRecord)*record,
                            std::make_unique<base::Value>(record->GetData()));
  if (status)
    status = biod_storage.ReadRecordsForSingleUser(user_id);

  return 0;
}
