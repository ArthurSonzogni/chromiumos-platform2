// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <memory>
#include <utility>

#include <base/base64.h>
#include <base/dcheck_is_on.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/logging.h>
#include <base/test/bind.h>
#include <brillo/compression/compressor_interface.h>
#include <brillo/compression/mock_compressor.h>
#include <brillo/strings/string_utils.h>
#include <chromeos/dbus/service_constants.h>
#include <dbus/mock_bus.h>
#include <dbus/mock_exported_object.h>
#include <dbus/mock_object_proxy.h>
#include <gtest/gtest.h>

#include "featured/mock_tmp_storage_impl.h"
#include "featured/service.h"
#include "featured/store_impl.h"
#include "featured/store_impl_mock.h"
#include "featured/store_interface.h"

using ::testing::_;
using ::testing::ElementsAre;
using ::testing::Invoke;
using ::testing::Return;
using ::testing::StrictMock;

namespace featured {

namespace {
const char kTestData[] = "test";
// base64-encoded string of kTestData.
const char kTestEncodedData[] = "dGVzdA==";

const char kTestDifferentData[] = "different";
// base64-encoded string of kTestDifferentData.
const char kTestDifferentEncodedData[] = "ZGlmZmVyZW50";

void ResponseSenderCallback(const std::string& expected_message,
                            std::unique_ptr<dbus::Response> response) {
  EXPECT_EQ(expected_message, response->ToString());
}

TEST(SupportCheckCommand, FileExistsTest) {
  base::FilePath file;
  ASSERT_TRUE(base::CreateTemporaryFile(&file));

  FileExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.IsSupported());

  FileNotExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.IsSupported());
}

TEST(SupportCheckCommand, FileNotExistsTest) {
  base::ScopedTempDir dir;
  ASSERT_TRUE(dir.CreateUniqueTempDir());

  base::FilePath file(dir.GetPath().Append("non-existent"));

  FileNotExistsCommand c(file.MaybeAsASCII());
  ASSERT_TRUE(c.IsSupported());

  FileExistsCommand c2(file.MaybeAsASCII());
  ASSERT_FALSE(c2.IsSupported());
}

// Verify that the AlwaysSupported command is always supported.
TEST(SupportCheckCommand, AlwaysSupported) {
  ASSERT_TRUE(AlwaysSupportedCommand().IsSupported());
}

// Verify that Mkdir succeeds in a basic case.
TEST(FeatureCommand, Mkdir_Allowed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("sys")));

  base::FilePath sys_path(temp_dir.GetPath().Append("sys/foo"));
  EXPECT_FALSE(base::PathExists(sys_path));

  MkdirCommand mkdir("/sys/foo");
  mkdir.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_TRUE(mkdir.Execute());
  EXPECT_TRUE(base::PathExists(sys_path));

  // Executing *twice* should succeed since the path already exists.
  EXPECT_TRUE(mkdir.Execute());
}

// Verify that Mkdir fails if the prefix isn't allowed.
TEST(FeatureCommand, Mkdir_NotAllowed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("mnt")));

  base::FilePath mnt_path(temp_dir.GetPath().Append("mnt/foo"));
  EXPECT_FALSE(base::PathExists(mnt_path));

  MkdirCommand mkdir("/mnt/foo");
  mkdir.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_FALSE(base::PathExists(mnt_path));
  EXPECT_FALSE(mkdir.Execute());
  EXPECT_FALSE(base::PathExists(mnt_path));
}

// Verify that Mkdir fails if directory creation fails.
TEST(FeatureCommand, Mkdir_CreateFails) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("sys")));

  base::FilePath sys_path(temp_dir.GetPath().Append("sys/foo"));
  ASSERT_TRUE(base::WriteFile(sys_path, "2"));

  MkdirCommand mkdir("/sys/foo");
  mkdir.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_TRUE(base::PathExists(sys_path));
  EXPECT_FALSE(mkdir.Execute());
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(sys_path, &contents));
  EXPECT_EQ(contents, "2");
}

// Verify that WriteFile succeeds in a basic case.
TEST(FeatureCommand, WriteFile_Success) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("sys")));

  base::FilePath sys_path(temp_dir.GetPath().Append("sys/foo"));
  EXPECT_FALSE(base::PathExists(sys_path));

  WriteFileCommand write("/sys/foo", "1");
  write.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_TRUE(write.Execute());
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(sys_path, &contents));
  EXPECT_EQ(contents, "1");
}

// Verify that WriteFile fails if the prefix isn't allowed.
TEST(FeatureCommand, WriteFile_NotAllowed) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("mnt")));

  base::FilePath mnt_path(temp_dir.GetPath().Append("mnt/foo"));
  EXPECT_FALSE(base::PathExists(mnt_path));

  WriteFileCommand write("/mnt/foo", "1");
  write.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
  EXPECT_FALSE(write.Execute());
  EXPECT_FALSE(base::PathExists(base::FilePath(mnt_path)));
}

// Verify that WriteFile fails if file writing fails.
TEST(FeatureCommand, WriteFile_Fails) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  // Do *not* create the sys directory.

  base::FilePath sys_path(temp_dir.GetPath().Append("sys/foo"));
  EXPECT_FALSE(base::PathExists(sys_path));

  WriteFileCommand write("/sys/foo", "1");
  write.SetPrefixForTesting(temp_dir.GetPath());

  EXPECT_FALSE(write.Execute());
  EXPECT_FALSE(base::PathExists(base::FilePath(sys_path)));
}

// Verify that PlatformFeature::Execute runs all commands.
TEST(PlatformFeature, ExecuteBasic) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("sys")));

  base::FilePath sys_dir_path(temp_dir.GetPath().Append("sys/foo"));
  EXPECT_FALSE(base::PathExists(sys_dir_path));
  base::FilePath sys_file_path(temp_dir.GetPath().Append("sys/foo/bar"));
  EXPECT_FALSE(base::PathExists(sys_file_path));

  auto mkdir = std::make_unique<MkdirCommand>("/sys/foo");
  mkdir->SetPrefixForTesting(temp_dir.GetPath());
  auto write = std::make_unique<WriteFileCommand>("/sys/foo/bar", "1");
  write->SetPrefixForTesting(temp_dir.GetPath());

  std::vector<std::unique_ptr<FeatureCommand>> commands;
  commands.push_back(std::move(mkdir));
  commands.push_back(std::move(write));

  PlatformFeature foo("foo", {}, std::move(commands));

  EXPECT_TRUE(foo.Execute());
  std::string contents;
  EXPECT_TRUE(base::ReadFileToString(sys_file_path, &contents));
  EXPECT_EQ(contents, "1");
}

// Verify that PlatformFeature::Execute stops as soon as one command fails.
TEST(PlatformFeature, ExecuteFail) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  ASSERT_TRUE(base::CreateDirectory(temp_dir.GetPath().Append("sys")));

  base::FilePath sys_dir_path(temp_dir.GetPath().Append("sys/foo/bar"));
  EXPECT_FALSE(base::PathExists(sys_dir_path));
  base::FilePath sys_file_path(temp_dir.GetPath().Append("sys/baz"));
  EXPECT_FALSE(base::PathExists(sys_file_path));

  auto write = std::make_unique<WriteFileCommand>("/sys/foo/bar", "1");
  write->SetPrefixForTesting(temp_dir.GetPath());
  auto mkdir = std::make_unique<MkdirCommand>("/sys/baz");
  mkdir->SetPrefixForTesting(temp_dir.GetPath());

  std::vector<std::unique_ptr<FeatureCommand>> commands;
  commands.push_back(std::move(write));
  commands.push_back(std::move(mkdir));

  PlatformFeature foo("foo", {}, std::move(commands));

  EXPECT_FALSE(foo.Execute());
  EXPECT_FALSE(base::PathExists(sys_file_path));
  EXPECT_FALSE(base::PathExists(sys_dir_path));
}

// Test that IsSupported returns true if all commands return true.
TEST(PlatformFeature, IsSupported) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath foo = temp_dir.GetPath().Append("foo");
  ASSERT_TRUE(base::CreateDirectory(foo));
  EXPECT_TRUE(base::PathExists(foo));
  base::FilePath bar = temp_dir.GetPath().Append("bar");
  EXPECT_FALSE(base::PathExists(bar));

  auto exist = std::make_unique<FileExistsCommand>(foo.value());
  auto not_exist = std::make_unique<FileNotExistsCommand>(bar.value());

  std::vector<std::unique_ptr<SupportCheckCommand>> commands;
  commands.push_back(std::move(exist));
  commands.push_back(std::move(not_exist));

  PlatformFeature features("foo", std::move(commands), {});

  EXPECT_TRUE(features.IsSupported());
}

// Test that IsSupported returns false if one command returns false.
TEST(PlatformFeature, IsSupported_Unsupported) {
  base::ScopedTempDir temp_dir;
  ASSERT_TRUE(temp_dir.CreateUniqueTempDir());
  base::FilePath foo = temp_dir.GetPath().Append("foo");
  ASSERT_TRUE(base::CreateDirectory(foo));
  EXPECT_TRUE(base::PathExists(foo));
  base::FilePath bar = temp_dir.GetPath().Append("bar");
  EXPECT_FALSE(base::PathExists(bar));

  auto exist_foo = std::make_unique<FileExistsCommand>(foo.value());
  auto exist_bar = std::make_unique<FileExistsCommand>(bar.value());

  std::vector<std::unique_ptr<SupportCheckCommand>> commands;
  // This has to be first to make sure we short circuit
  commands.push_back(std::move(exist_bar));
  commands.push_back(std::move(exist_foo));

  PlatformFeature features("foo", std::move(commands), {});

  EXPECT_FALSE(features.IsSupported());
}

// Verify that platform-features.json parses and has a feature used for
// integration tests.
TEST(JsonParser, PlatformFeaturesJsonParses) {
  const char kFeatureFileName[] = "share/platform-features.json";
  base::FilePath platform_features =
      base::FilePath(getenv("SRC")).Append(kFeatureFileName);

  std::string contents;
  ASSERT_TRUE(base::ReadFileToString(platform_features, &contents));

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.AreFeaturesParsed());
  ASSERT_TRUE(parser.ParseFileContents(contents));
  ASSERT_TRUE(parser.AreFeaturesParsed());

  const FeatureParserBase::FeatureMap* map = parser.GetFeatureMap();
  auto it = map->find("CrOSLateBootTestFeature");
  ASSERT_NE(it, map->end());
  EXPECT_EQ(it->second.name(), "CrOSLateBootTestFeature");

  std::vector<std::string> support_cmds =
      it->second.SupportCheckCommandNamesForTesting();
  EXPECT_THAT(support_cmds, ElementsAre("FileExists"));

  std::vector<std::string> exec_cmds = it->second.ExecCommandNamesForTesting();
  EXPECT_THAT(exec_cmds, ElementsAre("WriteFile"));
}

// Verify that the json parsing succeeds in a basic case.
TEST(JsonParser, Success_Basic) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir", "path": "/c/d"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_TRUE(parser.ParseFileContents(contents));
  ASSERT_TRUE(parser.AreFeaturesParsed());
}

// Verify that the json parsing succeeds without support_check_commands.
TEST(JsonParser, Success_NoSupportCommands) {
  std::string contents = R"([{
    "name": "foo",
    "commands" : [{"name": "Mkdir", "path": "/a/b"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_TRUE(parser.ParseFileContents(contents));
  ASSERT_TRUE(parser.AreFeaturesParsed());
}

// Verify that the json parsing succeeds with multiple commands.
TEST(JsonParser, Success_MultiCommands) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir", "path": "/c/d"},
                  {"name": "Mkdir", "path": "/e/f"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_TRUE(parser.ParseFileContents(contents));
  ASSERT_TRUE(parser.AreFeaturesParsed());
}

// Verify that the json parsing succeeds with multiple support-check commands.
TEST(JsonParser, Success_MultiSupportChecks) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"},
                               {"name": "FileNotExists", "path": "/c/d"}],
    "commands" : [{"name": "Mkdir", "path": "/e/f"},
                  {"name": "Mkdir", "path": "/g/h"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_TRUE(parser.ParseFileContents(contents));
  ASSERT_TRUE(parser.AreFeaturesParsed());
}

// Verify that invalid json doesn't parse.
TEST(JsonParser, Invalid_JsonParse) {
  std::string contents = "{";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that json that isn't a list at the top level doesn't parse.
TEST(JsonParser, Invalid_NotList) {
  std::string contents = "{}";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that an empty list doesn't parse.
TEST(JsonParser, Invalid_EmptyList) {
  std::string contents = "[]";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a list of something that isn't a dict doesn't parse.
TEST(JsonParser, Invalid_ListOfNotDict) {
  std::string contents = "[1, 2]";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a feature missing a name doesn't parse.
TEST(JsonParser, Invalid_MissingFeatureName) {
  std::string contents = R"([{
    "notName": "foo",
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands that isn't a list fails to parse.
TEST(JsonParser, Invalid_SupportCommandsNotList) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": 1,
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands that isn't a list of dicts fails.
TEST(JsonParser, Invalid_SupportCommandsNotListOfDict) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [1],
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands that is missing a name fails.
TEST(JsonParser, Invalid_SupportCommandsNoName) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"notName": "foo", "path": "/a/b"}],
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands that is has an invalid name fails.
TEST(JsonParser, Invalid_SupportCommandsBadName) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "foo", "path": "/a/b"}],
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands with FileExists and no path fails.
TEST(JsonParser, Invalid_SupportCommandsFileExistsNoPath) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists"}],
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a support_check_commands with FileNotExists and no path fails.
TEST(JsonParser, Invalid_SupportCommandsFileNotExistsNoPath) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileNotExists"}],
    "commands" : [{"name": "WriteFile", "path": "/a/b", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that a missing commands entry fails to parse.
TEST(JsonParser, Invalid_NoCommands) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if it's a list.
TEST(JsonParser, Invalid_CommandsInt) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands": 1
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if it's a non-empty list.
TEST(JsonParser, Invalid_CommandsEmpty) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands": []
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if it's a list of dicts
TEST(JsonParser, Invalid_CommandsNotDict) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands": [1]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if commands have names.
TEST(JsonParser, Invalid_CommandMissingName) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"notName": "WriteFile", "path": "/c/d", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if commands have valid names.
TEST(JsonParser, Invalid_CommandInvalidName) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "invalid", "path": "/c/d", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if WriteFile has a path.
TEST(JsonParser, Invalid_CommandWriteFileNoPath) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "WriteFile", "value": "1"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if WriteFile has a value.
TEST(JsonParser, Invalid_CommandWriteFileNoValue) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "WriteFile", "path": "/a/b"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if WriteFile has a value.
TEST(JsonParser, Invalid_CommandMkdirNoPath) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that commands only parses if all commands are valid.
TEST(JsonParser, Invalid_OneCommand) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir", "path": "/c/d"}, {"name": "invalid"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

// Verify that the json only parses without duplicate names.
TEST(JsonParser, Invalid_DuplicateNames) {
  std::string contents = R"([{
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir", "path": "/c/d"}]
  },
  {
    "name": "foo",
    "support_check_commands": [{"name": "FileExists", "path": "/a/b"}],
    "commands" : [{"name": "Mkdir", "path": "/c/d"}]
  }])";

  JsonFeatureParser parser;
  ASSERT_FALSE(parser.ParseFileContents(contents));
  ASSERT_FALSE(parser.AreFeaturesParsed());
}

}  // namespace

// A base class to set up dbus objects, etc, needed for all tests.
// Outside of the anonymous namespace to allow `friend class` and access to
// the private HandleSeedFetched method.
class DbusFeaturedServiceTestBase : public testing::Test {
 public:
  DbusFeaturedServiceTestBase(
      std::unique_ptr<MockStoreImpl> mock_store_impl,
      std::unique_ptr<MockTmpStorageImpl> mock_tmp_storage_impl)
      : mock_bus_(base::MakeRefCounted<dbus::MockBus>(dbus::Bus::Options{})),
        path_(chromeos::kChromeFeaturesServicePath),
        mock_proxy_(base::MakeRefCounted<dbus::MockObjectProxy>(
            mock_bus_.get(), chromeos::kChromeFeaturesServiceName, path_)),
        mock_exported_object_(base::MakeRefCounted<dbus::MockExportedObject>(
            mock_bus_.get(), path_)) {
    // This weird ownership structure is necessary to be able to run
    // EXPECT_CALLS/ON_CALLS in individual tests. The DbusFeaturedService class
    // will take ownership.
    mock_store_impl_ = mock_store_impl.get();
    mock_tmp_storage_impl_ = mock_tmp_storage_impl.get();
    std::unique_ptr<brillo::MockCompressor> mock_decompressor =
        std::make_unique<brillo::MockCompressor>();
    mock_decompressor_ = mock_decompressor.get();
    service_ = std::make_shared<DbusFeaturedService>(
        std::move(mock_store_impl), std::move(mock_tmp_storage_impl),
        std::move(mock_decompressor));

    ON_CALL(*mock_bus_, GetExportedObject(_))
        .WillByDefault(Return(mock_exported_object_.get()));
    ON_CALL(*mock_bus_, Connect()).WillByDefault(Return(true));
    ON_CALL(*mock_bus_, GetObjectProxy(_, _))
        .WillByDefault(Return(mock_proxy_.get()));
    ON_CALL(*mock_bus_, RequestOwnershipAndBlock(_, _))
        .WillByDefault(Return(true));
    ON_CALL(*mock_exported_object_, ExportMethodAndBlock(_, _, _))
        .WillByDefault(Return(true));
  }

  void TearDown() override {
    mock_bus_->ShutdownAndBlock();
    feature::PlatformFeatures::ShutdownForTesting();
  }

 protected:
  void HandleSeedFetched(dbus::MethodCall* method_call,
                         dbus::ExportedObject::ResponseSender sender) {
    service_->HandleSeedFetched(method_call, std::move(sender));
  }

  scoped_refptr<dbus::MockBus> mock_bus_;
  dbus::ObjectPath path_;
  scoped_refptr<dbus::MockObjectProxy> mock_proxy_;
  scoped_refptr<dbus::MockExportedObject> mock_exported_object_;
  MockStoreImpl* mock_store_impl_;
  MockTmpStorageImpl* mock_tmp_storage_impl_;
  brillo::MockCompressor* mock_decompressor_;
  std::shared_ptr<DbusFeaturedService> service_;
};

class DbusFeaturedServiceTest : public DbusFeaturedServiceTestBase {
 public:
  DbusFeaturedServiceTest()
      : DbusFeaturedServiceTestBase(std::make_unique<MockStoreImpl>(),
                                    std::make_unique<MockTmpStorageImpl>()) {
    ON_CALL(*mock_store_impl_, SetLastGoodSeed(_)).WillByDefault(Return(true));
    ON_CALL(*mock_store_impl_, ClearBootAttemptsSinceLastUpdate())
        .WillByDefault(Return(true));
  }
};

// Checks that service start successfully increments the boot attempts counter
// on boot.
TEST_F(DbusFeaturedServiceTest, IncrementBootAttemptsOnStartup_Success) {
  EXPECT_CALL(*mock_store_impl_, IncrementBootAttemptsSinceLastUpdate())
      .WillOnce(Return(true));

  EXPECT_TRUE(service_->Start(mock_bus_.get(), service_));
}

// Checks that service start fails when incrementing the boot attempts counter
// on boot fails.
TEST_F(DbusFeaturedServiceTest, IncrementBootAttemptsOnStartup_Failure) {
  EXPECT_CALL(*mock_store_impl_, IncrementBootAttemptsSinceLastUpdate())
      .WillOnce(Return(false));

  EXPECT_FALSE(service_->Start(mock_bus_.get(), service_));
}

// Checks that an empty response is returned on success, and that the store's
// SetLastGoodSeed method is called when the used seed matches safe seed.
TEST_F(DbusFeaturedServiceTest, HandleSeedFetched_Success_MatchedSeed) {
  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_METHOD_RETURN
reply_serial: 123

)--";

  SeedDetails used;
  used.set_b64_compressed_data(kTestEncodedData);
  EXPECT_CALL(*mock_tmp_storage_impl_, GetUsedSeedDetails())
      .WillOnce(Return(used));
  EXPECT_CALL(*mock_store_impl_, SetLastGoodSeed(_)).WillOnce(Return(true));
  EXPECT_CALL(*mock_decompressor_, Process(_, _))
      .Times(2)
      .WillRepeatedly(
          Return(brillo::string_utils::GetStringAsBytes(kTestData)));

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  // Should match |used|.
  SeedDetails seed;
  seed.set_b64_compressed_data(kTestEncodedData);
  writer.AppendProtoAsArrayOfBytes(seed);
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

// Checks that an empty response is returned on success, and that the store's
// SetLastGoodSeed method isn't called when used seed doesn't match safe seed.
TEST_F(DbusFeaturedServiceTest, HandleSeedFetched_Success_MismatchedSeed) {
  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_METHOD_RETURN
reply_serial: 123

)--";

  SeedDetails used;
  used.set_b64_compressed_data(kTestEncodedData);
  EXPECT_CALL(*mock_tmp_storage_impl_, GetUsedSeedDetails())
      .WillOnce(Return(used));
  EXPECT_CALL(*mock_store_impl_, SetLastGoodSeed(_)).Times(0);
  EXPECT_CALL(*mock_decompressor_, Process(_, _))
      .WillOnce(Return(brillo::string_utils::GetStringAsBytes(kTestData)))
      .WillOnce(
          Return(brillo::string_utils::GetStringAsBytes(kTestDifferentData)));

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  // Should be different than |used|.
  SeedDetails seed;
  seed.set_b64_compressed_data(kTestDifferentEncodedData);
  writer.AppendProtoAsArrayOfBytes(seed);
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

// Checks that HandleSeedFetched returns an error response when no arguments are
// passed in.
TEST_F(DbusFeaturedServiceTest, HandleSeedFetched_Failure_NoArgument) {
  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_ERROR
error_name: org.freedesktop.DBus.Error.InvalidArgs
signature: s
reply_serial: 123

string "Could not parse seed argument"
)--";

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

// Checks that HandleSeedFetched returns an error response when a non-seed
// argument is passed in.
TEST_F(DbusFeaturedServiceTest, HandleSeedFetched_Failure_InvalidArgument) {
  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_ERROR
error_name: org.freedesktop.DBus.Error.InvalidArgs
signature: s
reply_serial: 123

string "Could not parse seed argument"
)--";

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  writer.AppendString("string");
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

// Checks that HandleSeedFetched returns an error response when saving the seed
// to disk fails.
TEST_F(DbusFeaturedServiceTest, HandleSeedFetched_Failure_SetSeedFailure) {
  EXPECT_CALL(*mock_store_impl_, SetLastGoodSeed(_)).WillOnce(Return(false));

  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_ERROR
error_name: org.freedesktop.DBus.Error.Failed
signature: s
reply_serial: 123

string "Failed to write fetched seed to disk"
)--";

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  SeedDetails seed;
  writer.AppendProtoAsArrayOfBytes(seed);
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

// Checks that HandleSeedFetched returns an error response when saving the seed
// to disk fails.
TEST_F(DbusFeaturedServiceTest,
       HandleSeedFetched_Failure_ClearBootCounterFailure) {
  EXPECT_CALL(*mock_store_impl_, ClearBootAttemptsSinceLastUpdate())
      .WillOnce(Return(false));

  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_ERROR
error_name: org.freedesktop.DBus.Error.Failed
signature: s
reply_serial: 123

string "Failed to reset boot attempts counter"
)--";

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  SeedDetails seed;
  writer.AppendProtoAsArrayOfBytes(seed);
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}

class DbusFeaturedServiceNoLockboxTest : public DbusFeaturedServiceTestBase {
 public:
  DbusFeaturedServiceNoLockboxTest()
      : DbusFeaturedServiceTestBase(nullptr, nullptr) {}
};

TEST_F(DbusFeaturedServiceNoLockboxTest, Startup_Success) {
  EXPECT_TRUE(service_->Start(mock_bus_.get(), service_));
}

// Checks that an empty response is returned on missing store.
TEST_F(DbusFeaturedServiceNoLockboxTest, HandleSeedFetched_Success) {
  constexpr char kExpectedMessage[] = R"--(message_type: MESSAGE_METHOD_RETURN
reply_serial: 123

)--";

  dbus::MethodCall method_call("com.example.Interface", "SomeMethod");
  dbus::MessageWriter writer(&method_call);
  SeedDetails seed;
  writer.AppendProtoAsArrayOfBytes(seed);
  // Not setting the serial causes a crash.
  method_call.SetSerial(123);

  HandleSeedFetched(&method_call,
                    base::BindOnce(&ResponseSenderCallback, kExpectedMessage));
}
}  // namespace featured
