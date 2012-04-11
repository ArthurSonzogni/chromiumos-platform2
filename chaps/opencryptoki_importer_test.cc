// Copyright (c) 2012 The Chromium OS Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chaps/opencryptoki_importer.h"

#include <stdlib.h>

#include <map>
#include <string>
#include <vector>

#include <base/stringprintf.h>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "chaps/chaps_factory_mock.h"
#include "chaps/object_mock.h"
#include "chaps/object_pool_mock.h"
#include "chaps/tpm_utility_mock.h"

using std::map;
using std::string;
using std::vector;
using testing::_;
using testing::AnyNumber;
using testing::DoAll;
using testing::Invoke;
using testing::Return;
using testing::SetArgumentPointee;
using testing::Values;

namespace chaps {

const unsigned char kSampleMasterKeyEncrypted[] = {
    80, 118, 191, 150, 143, 171, 162, 61, 89, 32, 95, 219, 44, 244, 51, 84, 117,
    228, 36, 225, 240, 122, 234, 92, 182, 224, 133, 238, 100, 18, 116, 130, 166,
    177, 7, 103, 223, 122, 112, 136, 126, 30, 191, 253, 137, 85, 70, 187, 220,
    137, 248, 155, 89, 152, 113, 153, 113, 48, 59, 148, 246, 114, 146, 13, 86,
    254, 227, 3, 229, 70, 247, 165, 101, 76, 3, 58, 134, 230, 84, 113, 94, 226,
    134, 130, 34, 100, 56, 157, 5, 255, 127, 180, 147, 56, 43, 233, 32, 254,
    209, 52, 41, 48, 15, 127, 110, 187, 183, 254, 123, 20, 182, 153, 107, 192,
    136, 229, 72, 243, 38, 238, 155, 59, 216, 15, 17, 72, 39, 209, 196, 66, 53,
    140, 236, 132, 19, 69, 58, 107, 103, 22, 19, 70, 175, 35, 126, 16, 56, 132,
    150, 89, 182, 12, 3, 166, 206, 160, 194, 12, 250, 211, 141, 73, 109, 83,
    144, 253, 166, 71, 109, 219, 143, 202, 237, 89, 185, 136, 249, 104, 78, 68,
    11, 169, 144, 194, 57, 140, 147, 104, 175, 229, 20, 223, 98, 109, 187, 120,
    200, 126, 81, 147, 31, 13, 239, 36, 233, 221, 78, 117, 59, 248, 156, 231,
    189, 232, 48, 128, 150, 128, 84, 244, 30, 117, 183, 150, 70, 30, 234, 2,
    233, 161, 120, 96, 185, 155, 34, 75, 173, 200, 78, 183, 66, 8, 144, 72, 20,
    92, 246, 229, 255, 55, 148, 160, 153, 9, 150, 16};

const unsigned char kSampleMasterKey[] = {
    116, 62, 77, 252, 196, 57, 225, 14, 115, 52, 68, 60, 227, 254, 22, 162, 163,
    22, 186, 125, 203, 138, 205, 98, 151, 202, 179, 203, 86, 98, 149, 208};

const unsigned char kSampleAuthDataEncrypted[] = {
    37, 239, 160, 111, 19, 123, 167, 118, 161, 223, 61, 242, 63, 146, 22, 223,
    100, 79, 178, 52, 206, 121, 155, 88, 23, 68, 144, 66, 167, 187, 83, 13, 101,
    221, 218, 185, 99, 23, 149, 3, 239, 142, 78, 62, 239, 155, 114, 83, 106,
    108, 168, 225, 241, 58, 49, 59, 235, 234, 51, 92, 241, 75, 120, 26, 8, 36,
    238, 241, 33, 192, 170, 136, 138, 57, 87, 210, 181, 143, 111, 181, 251, 30,
    50, 64, 48, 96, 195, 223, 172, 221, 19, 127, 253, 182, 102, 219, 36, 245,
    246, 106, 157, 177, 230, 129, 130, 253, 51, 91, 214, 35, 221, 43, 174, 7,
    185, 169, 92, 126, 52, 160, 212, 233, 158, 142, 120, 255, 212, 32, 10, 176,
    112, 73, 71, 51, 72, 143, 218, 157, 186, 106, 146, 71, 24, 94, 216, 98, 114,
    127, 56, 47, 38, 35, 63, 141, 193, 82, 107, 240, 39, 154, 28, 134, 32, 96,
    16, 32, 54, 233, 74, 242, 136, 178, 236, 0, 243, 5, 78, 98, 219, 0, 104, 70,
    235, 248, 169, 38, 88, 129, 219, 84, 197, 53, 232, 186, 157, 6, 24, 161, 86,
    118, 85, 227, 72, 215, 30, 64, 236, 224, 234, 168, 16, 118, 4, 154, 170,
    157, 85, 80, 158, 87, 14, 17, 76, 15, 11, 151, 157, 15, 42, 92, 34, 255,
    244, 162, 195, 158, 162, 207, 167, 119, 9, 218, 218, 148, 33, 54, 131, 66,
    125, 12, 141, 245, 162, 229, 134, 227};

const unsigned char kSampleAuthData[] = {
    29, 230, 13, 53, 202, 172, 136, 59, 83, 139, 43, 154, 175, 183, 163, 205,
    110, 117, 149, 144};

const char kTokenBasePath[] = "/tmp/chaps_unit_test";
const char kTokenPath[] = "/tmp/chaps_unit_test/.tpm";
const char kTokenObjectPath[] = "/tmp/chaps_unit_test/.tpm/TOK_OBJ";
const char kSampleToken[] = "opencryptoki_sample_token.tgz";
const int kTotalSampleObjects = 5;

string Bytes2String(const unsigned char* bytes, size_t num_bytes) {
  return string(reinterpret_cast<const char*>(bytes), num_bytes);
}

// Performs hard-coded transformations as a TPM would do. These match the
// sample token data for this test, they are not useful in general.
bool MockUnbind(int key, const string& input, string* output) {
  map<string, string> transforms;
  string encrypted = Bytes2String(kSampleMasterKeyEncrypted,
                                  arraysize(kSampleMasterKeyEncrypted));
  string decrypted = Bytes2String(kSampleMasterKey,
                                  arraysize(kSampleMasterKey));
  transforms[encrypted] = decrypted;
  encrypted = Bytes2String(kSampleAuthDataEncrypted,
                           arraysize(kSampleAuthDataEncrypted));
  decrypted = Bytes2String(kSampleAuthData,
                           arraysize(kSampleAuthData));
  transforms[encrypted] = decrypted;

  map<string, string>::iterator iter = transforms.find(input);
  if (iter != transforms.end()) {
    *output = iter->second;
    return true;
  }
  return false;
}

// Creates a very 'nice' object mock.
Object* CreateObjectMock() {
  ObjectMock* o = new ObjectMock();
  o->SetupFake();
  EXPECT_CALL(*o, GetObjectClass()).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributes(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, FinalizeNewObject()).WillRepeatedly(Return(CKR_OK));
  EXPECT_CALL(*o, Copy(_)).WillRepeatedly(Return(CKR_OK));
  EXPECT_CALL(*o, IsTokenObject()).Times(AnyNumber());
  EXPECT_CALL(*o, IsPrivate()).Times(AnyNumber());
  EXPECT_CALL(*o, IsAttributePresent(_)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeString(_)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeInt(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeBool(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeString(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeInt(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, SetAttributeBool(_, _)).Times(AnyNumber());
  EXPECT_CALL(*o, GetAttributeMap()).Times(AnyNumber());
  EXPECT_CALL(*o, set_handle(_)).Times(AnyNumber());
  EXPECT_CALL(*o, set_store_id(_)).Times(AnyNumber());
  EXPECT_CALL(*o, handle()).Times(AnyNumber());
  EXPECT_CALL(*o, store_id()).Times(AnyNumber());
  return o;
}

// A test fixture base class for testing the importer.
class TestImporterBase {
 public:
  TestImporterBase() : importer_(0, &tpm_, &factory_) {
    // Set expectations for the TPM utility mock.
    EXPECT_CALL(tpm_, Unbind(_, _, _)).WillRepeatedly(Invoke(MockUnbind));
    EXPECT_CALL(tpm_, LoadKey(_, _, _, _))
        .WillRepeatedly(DoAll(SetArgumentPointee<3>(1), Return(true)));
    EXPECT_CALL(tpm_, LoadKeyWithParent(_, _, _, _, _))
        .WillRepeatedly(DoAll(SetArgumentPointee<4>(1), Return(true)));

    // Set expectations for the factory mock.
    EXPECT_CALL(factory_, CreateObject())
        .WillRepeatedly(Invoke(CreateObjectMock));

    // Set expectations for the object pool mock.
    pool_.SetupFake();
    EXPECT_CALL(pool_, Insert(_)).Times(AnyNumber());
    EXPECT_CALL(pool_, Find(_, _)).Times(AnyNumber());
    EXPECT_CALL(pool_, SetInternalBlob(3, _)).WillRepeatedly(Return(true));
    EXPECT_CALL(pool_, SetInternalBlob(4, _)).WillRepeatedly(Return(true));
  }

 protected:
  ChapsFactoryMock factory_;
  ObjectPoolMock pool_;
  OpencryptokiImporter importer_;
  TPMUtilityMock tpm_;
};

// A function that returns the number of objects expected to be imported.
// Returns -1 if a failure is expected.
typedef int (*ModifierCallback)();

// A parameterized fixture so we can run the same test(s) with multiple modifier
// functions.
class TestImporterWithModifier
    : public TestImporterBase,
      public testing::TestWithParam<ModifierCallback> {
};

void RunCommand(string command) {
  int status = system(command.c_str());
  ASSERT_EQ(0, WEXITSTATUS(status));
}

void PrepareSampleToken() {
  RunCommand(base::StringPrintf("mkdir -p %s", kTokenBasePath));
  RunCommand(base::StringPrintf("tar -xzf %s -C %s",
                                kSampleToken,
                                kTokenBasePath));
}

void CleanupSampleToken() {
  RunCommand(base::StringPrintf("rm -rf %s", kTokenBasePath));
}

// This test attempts to import a sample token after it has been modified by a
// modifier function.
TEST_P(TestImporterWithModifier, ImportSample) {
  PrepareSampleToken();
  ModifierCallback modifier = GetParam();
  int expected_objects = modifier();
  bool expected_result = (expected_objects >= 0);
  EXPECT_EQ(expected_result,
            importer_.ImportObjects(FilePath(kTokenPath), &pool_));
  CleanupSampleToken();
  if (expected_objects < 0)
    expected_objects = 0;
  vector<const Object*> objects;
  pool_.Find(NULL, &objects);
  EXPECT_EQ(expected_objects, objects.size());
}

int NoModify() {
  // If we don't modify anything, the import should succeed.
  return kTotalSampleObjects;
}

int DeleteAll() {
  RunCommand(base::StringPrintf("rm -rf %s", kTokenPath));
  return 0;
}

int DeleteAllObjectFiles() {
  RunCommand(base::StringPrintf("rm -f %s/*", kTokenObjectPath));
  return 0;
}

int DeleteMasterKey() {
  RunCommand(base::StringPrintf("rm -f %s/MK_PRIVATE", kTokenPath));
  return -1;
}

int DeleteObjectIndex() {
  RunCommand(base::StringPrintf("rm -f %s/OBJ.IDX", kTokenObjectPath));
  return 0;
}

int DeleteAllButIndex() {
  RunCommand(base::StringPrintf("rm -f %s/*0000", kTokenObjectPath));
  return 0;
}

int DeleteHierarchyFile() {
  RunCommand(base::StringPrintf("rm -f %s/10000000", kTokenObjectPath));
  return -1;
}

int TruncateFile0() {
  RunCommand(base::StringPrintf(":> %s/B0000000", kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int TruncateFile5() {
  RunCommand(base::StringPrintf("truncate -s 5 %s/B0000000", kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int TruncateFile21() {
  RunCommand(base::StringPrintf("truncate -s 21 %s/B0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int TruncateFile80() {
  RunCommand(base::StringPrintf("truncate -s 80 %s/B0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int TruncateEncrypted() {
  RunCommand(base::StringPrintf("truncate -s 80 %s/C0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int AddNotIndexed() {
  RunCommand(base::StringPrintf(":> %s/D0000000", kTokenObjectPath));
  return kTotalSampleObjects;
}

int AppendJunk() {
  RunCommand(base::StringPrintf("head -c 100 < /dev/urandom >> %s/B0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int AppendJunkEncrypted() {
  RunCommand(base::StringPrintf("head -c 100 < /dev/urandom >> %s/C0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

// List of parameterized test cases.
INSTANTIATE_TEST_CASE_P(ModifierTests,
                        TestImporterWithModifier,
                        Values(NoModify,
                               DeleteAll,
                               DeleteAllObjectFiles,
                               DeleteMasterKey,
                               DeleteObjectIndex,
                               DeleteAllButIndex,
                               DeleteHierarchyFile,
                               TruncateFile0,
                               TruncateFile5,
                               TruncateFile21,
                               TruncateFile80,
                               TruncateEncrypted,
                               AddNotIndexed,
                               AppendJunk,
                               AppendJunkEncrypted));

int RandomizeFile() {
  RunCommand(base::StringPrintf("head -c 1000 < /dev/urandom > %s/C0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

int RandomizeObjectAttributes() {
  RunCommand(base::StringPrintf("truncate -s 21 %s/B0000000",
                                kTokenObjectPath));
  RunCommand(base::StringPrintf("head -c 1000 < /dev/urandom >> %s/B0000000",
                                kTokenObjectPath));
  return kTotalSampleObjects - 1;
}

// List of test cases that involve randomization; these are listed seperately
// for easy filtering.
INSTANTIATE_TEST_CASE_P(RandomizedTests,
                        TestImporterWithModifier,
                        Values(RandomizeFile, RandomizeObjectAttributes));
}  // namespace chaps

int main(int argc, char** argv) {
  ::testing::InitGoogleMock(&argc, argv);
  return RUN_ALL_TESTS();
}
