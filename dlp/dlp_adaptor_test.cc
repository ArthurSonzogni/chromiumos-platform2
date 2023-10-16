// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <memory>
#include <poll.h>
#include <string>
#include <utility>

#include <base/files/file_util.h>
#include <base/files/scoped_file.h>
#include <base/files/scoped_temp_dir.h>
#include <base/functional/callback.h>
#include <base/memory/scoped_refptr.h>
#include <base/process/process_handle.h>
#include <base/run_loop.h>
#include "base/time/time.h"
#include <brillo/dbus/mock_dbus_method_response.h>
#include <brillo/files/file_util.h>
#include <gtest/gtest.h>

#include "dlp/dlp_adaptor_test_helper.h"
#include "dlp/file_id.h"

using testing::_;
using testing::ElementsAre;
using testing::Invoke;
using testing::Return;

namespace dlp {
namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
constexpr int kDBusSerial = 123;
constexpr int kPid = 1234;
constexpr int kIoTaskId = 12345;

class FileOpenRequestResultWaiter {
 public:
  FileOpenRequestResultWaiter() = default;
  ~FileOpenRequestResultWaiter() = default;
  FileOpenRequestResultWaiter(const FileOpenRequestResultWaiter&) = delete;
  FileOpenRequestResultWaiter& operator=(const FileOpenRequestResultWaiter&) =
      delete;

  // Waits until the result is available and returns it.
  bool GetResult() {
    run_loop_.Run();
    return result_;
  }

  // Returns the callback which should be passed to
  // DlpAdaptor::ProcessFileOpenRequest.
  base::OnceCallback<void(bool)> GetCallback() {
    return base::BindOnce(&FileOpenRequestResultWaiter::OnResult,
                          base::Unretained(this));
  }

 private:
  // Invoked when a result is available.
  void OnResult(bool result) {
    result_ = result;
    run_loop_.Quit();
  }

  base::RunLoop run_loop_;

  // Not initialized before run loop is quit.
  bool result_;
};

bool IsFdClosed(int fd) {
  struct pollfd pfd = {
      .fd = fd,
      .events = POLLERR,
  };
  if (poll(&pfd, 1, 1) < 0)
    return false;
  return pfd.revents & POLLERR;
}

// Parses a response message from a byte array.
template <typename TResponse>
TResponse ParseResponse(const std::vector<uint8_t>& response_blob) {
  TResponse response;
  EXPECT_TRUE(
      response.ParseFromArray(response_blob.data(), response_blob.size()));
  return response;
}

// Pseudo-random proto made of determined 10 bytes.
std::vector<uint8_t> RandomProtoBlob() {
  const size_t random_proto_size = 10;
  std::vector<uint8_t> proto_blob(random_proto_size);
  for (uint8_t i = 0; i < random_proto_size; ++i) {
    proto_blob.push_back(i);
  }
  return proto_blob;
}

}  // namespace

class DlpAdaptorTest : public ::testing::Test {
 public:
  DlpAdaptorTest() {
    // By passing true to SetFanotifyWatcherStartedForTesting,
    // DlpAdaptor won't try to start Fanotify. And given that these tests are
    // meant to test DlpAdaptor and don't depend on Fanotify, so Fanotify
    // initialisation isn't needed anyway.
    GetDlpAdaptor()->SetFanotifyWatcherStartedForTesting(true);
  }

  ~DlpAdaptorTest() override = default;

  DlpAdaptorTest(const DlpAdaptorTest&) = delete;
  DlpAdaptorTest& operator=(const DlpAdaptorTest&) = delete;

  DlpAdaptor* GetDlpAdaptor() { return helper_.adaptor(); }
  scoped_refptr<dbus::MockObjectProxy> GetMockDlpFilesPolicyServiceProxy() {
    return helper_.mock_dlp_files_policy_service_proxy();
  }

  AddFileRequest CreateAddFileRequest(const base::FilePath& path,
                                      const std::string& source,
                                      const std::string& referrer) {
    AddFileRequest request;
    request.set_file_path(path.value());
    request.set_source_url(source);
    request.set_referrer_url(referrer);
    return request;
  }

  std::vector<uint8_t> CreateSerializedAddFilesRequest(
      std::vector<AddFileRequest> add_file_requests) {
    AddFilesRequest request;
    *request.mutable_add_file_requests() = {add_file_requests.begin(),
                                            add_file_requests.end()};

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedRequestFileAccessRequest(
      std::vector<std::string> files_paths,
      int pid,
      const std::string& destination) {
    RequestFileAccessRequest request;
    *request.mutable_files_paths() = {files_paths.begin(), files_paths.end()};
    request.set_process_id(pid);
    request.set_destination_url(destination);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedRequestFileAccessRequest(
      std::vector<std::string> files_paths, int pid, DlpComponent component) {
    RequestFileAccessRequest request;
    *request.mutable_files_paths() = {files_paths.begin(), files_paths.end()};
    request.set_process_id(pid);
    request.set_destination_component(component);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedCheckFilesTransferRequest(
      std::vector<std::string> files_paths, const std::string& destination) {
    CheckFilesTransferRequest request;
    *request.mutable_files_paths() = {files_paths.begin(), files_paths.end()};
    request.set_destination_url(destination);
    request.set_file_action(FileAction::TRANSFER);
    request.set_io_task_id(kIoTaskId);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedCheckFilesTransferRequest(
      std::vector<std::string> files_paths, DlpComponent destination) {
    CheckFilesTransferRequest request;
    *request.mutable_files_paths() = {files_paths.begin(), files_paths.end()};
    request.set_destination_component(destination);
    request.set_file_action(FileAction::TRANSFER);
    request.set_io_task_id(kIoTaskId);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedGetFilesSourcesRequest(
      std::vector<ino64_t> inodes, std::vector<std::string> paths) {
    GetFilesSourcesRequest request;
    *request.mutable_files_inodes() = {inodes.begin(), inodes.end()};
    *request.mutable_files_paths() = {paths.begin(), paths.end()};

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  void StubIsDlpPolicyMatched(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    IsDlpPolicyMatchedResponse response_proto;
    response_proto.set_restricted(is_file_policy_restricted_);

    writer.AppendProtoAsArrayOfBytes(response_proto);
    std::move(*response_callback).Run(response.get());
  }

  void StubReplyWithError(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto error_response = dbus::ErrorResponse::FromMethodCall(
        method_call, "dlp.Error", "error message");
    std::move(*error_callback).Run(error_response.get());
  }

  void StubReplyBadProto(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    std::vector<uint8_t> proto_blob = RandomProtoBlob();
    writer.AppendArrayOfBytes(proto_blob.data(), proto_blob.size());
    std::move(*response_callback).Run(response.get());
  }

  void StubIsFilesTransferRestricted(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    dbus::MessageReader reader(method_call);
    IsFilesTransferRestrictedRequest request;
    EXPECT_TRUE(reader.PopArrayOfBytesAsProto(&request));

    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    IsFilesTransferRestrictedResponse response_proto;
    for (const auto& file : request.transferred_files()) {
      for (const auto& [file_metadata, restriction_level] :
           files_restrictions_) {
        if (file.inode() == file_metadata.inode()) {
          FileRestriction* file_restriction =
              response_proto.add_files_restrictions();
          *file_restriction->mutable_file_metadata() = file_metadata;
          file_restriction->set_restriction_level(restriction_level);
          break;
        }
      }
    }

    writer.AppendProtoAsArrayOfBytes(response_proto);
    std::move(*response_callback).Run(response.get());
  }

  void AddFilesAndCheck(const std::vector<AddFileRequest>& add_file_requests,
                        bool expected_result) {
    bool success;
    std::unique_ptr<
        brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>
        response = std::make_unique<
            brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
            nullptr);
    base::RunLoop run_loop;
    response->set_return_callback(base::BindOnce(
        [](bool* success, base::RunLoop* run_loop,
           const std::vector<uint8_t>& proto_blob) {
          AddFilesResponse response =
              ParseResponse<AddFilesResponse>(proto_blob);
          *success = response.error_message().empty();
          run_loop->Quit();
        },
        &success, &run_loop));
    GetDlpAdaptor()->AddFiles(
        std::move(response),
        CreateSerializedAddFilesRequest(add_file_requests));
    run_loop.Run();
    EXPECT_EQ(expected_result, success);
  }

  GetFilesSourcesResponse GetFilesSources(std::vector<ino64_t> inodes,
                                          std::vector<std::string> paths) {
    GetFilesSourcesResponse result;
    std::unique_ptr<
        brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>
        response = std::make_unique<
            brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
            nullptr);
    base::RunLoop run_loop;
    response->set_return_callback(base::BindOnce(
        [](GetFilesSourcesResponse* result, base::RunLoop* run_loop,
           const std::vector<uint8_t>& proto_blob) {
          *result = ParseResponse<GetFilesSourcesResponse>(proto_blob);
          run_loop->Quit();
        },
        &result, &run_loop));

    GetDlpAdaptor()->GetFilesSources(
        std::move(response),
        CreateSerializedGetFilesSourcesRequest(inodes, paths));
    run_loop.Run();
    return result;
  }

  void InitDatabase() {
    database_directory_ = std::make_unique<base::ScopedTempDir>();
    ASSERT_TRUE(database_directory_->CreateUniqueTempDir());
    base::RunLoop run_loop;
    GetDlpAdaptor()->InitDatabase(database_directory_->GetPath(),
                                  run_loop.QuitClosure());
    run_loop.Run();
  }

  void SetRulesAndInitFanotify() {
    GetDlpAdaptor()->SetFanotifyWatcherStartedForTesting(false);
    SetDlpFilesPolicyRequest request;
    ::dlp::DlpFilesRule* rule = request.add_rules();
    rule->add_source_urls("example.com");
    rule->add_destination_urls("*");
    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    std::vector<uint8_t> response_blob =
        GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

    SetDlpFilesPolicyResponse response =
        ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

    EXPECT_FALSE(response.has_error_message());
    EXPECT_TRUE(helper_.IsFanotifyWatcherActive());
  }

 protected:
  bool is_file_policy_restricted_;
  std::vector<std::pair<FileMetadata, RestrictionLevel>> files_restrictions_;
  std::unique_ptr<base::ScopedTempDir> database_directory_;

  DlpAdaptorTestHelper helper_;
};

TEST_F(DlpAdaptorTest, AllowedWithoutDatabase) {
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest({/*inode=*/1, /*crtime=*/0}, kPid,
                                 waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kDatabaseNotReadyError)));
}

TEST_F(DlpAdaptorTest, AllowedWithDatabase) {
  InitDatabase();

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest({/*inode=*/1, /*crtime=*/0}, kPid,
                                 waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, NotRestrictedFileAddedAndAllowed) {
  InitDatabase();

  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  FileId id = GetFileId(file_path.value());

  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, NotRestrictedFileAddedAndDlpPolicyMatched_BadProto) {
  InitDatabase();

  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  FileId id = GetFileId(file_path.value());

  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyBadProto));

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest,
       NotRestrictedFileAddedAndDlpPolicyMatched_ResponseError) {
  InitDatabase();

  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  FileId id = GetFileId(file_path.value());

  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyWithError));

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kRestrictionDetectionError)));
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndNotAllowed) {
  InitDatabase();

  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  FileId id = GetFileId(file_path.value());

  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileAllowedForItself) {
  InitDatabase();

  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  FileId id = GetFileId(file_path.value());

  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, base::GetCurrentProcId(),
                                 waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

//
TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedAllowed) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request access by ids.
  base::FilePath file_path1;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1);
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2);
  const FileId id2 = GetFileId(file_path2.value());

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, "source", "referrer"),
                    CreateAddFileRequest(file_path2, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(
          {file_path1.value(), file_path2.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);
  EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

  // Access the first file.
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());

  // Second request still allowed.
  FileOpenRequestResultWaiter waiter2;
  helper_.ProcessFileOpenRequest(id1, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());

  // Access the second file.
  FileOpenRequestResultWaiter waiter3;
  helper_.ProcessFileOpenRequest(id2, kPid, waiter3.GetCallback());

  EXPECT_TRUE(waiter3.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

// Cached allow response had no access grant attached to its ScopedFD.
// This test makes sure this doesn't happen anymore.
// http://b/281497666
TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedCachedAllowed) {
  InitDatabase();

  // Create files to request access by ids.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  FileMetadata file_metadata;
  file_metadata.set_inode(id.first);
  file_metadata.set_crtime(id.second);
  file_metadata.set_path(file_path.value());
  files_restrictions_.push_back(
      {std::move(file_metadata), RestrictionLevel::LEVEL_ALLOW});
  ON_CALL(*GetMockDlpFilesPolicyServiceProxy(),
          DoCallMethodWithErrorCallback(_, _, _, _))
      .WillByDefault(
          Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));
  // Called for the first RequestFileAccess and both ProcessFileOpen after the
  // closed ScopedFD. The second RequestFileAccess is cached.
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(3);

  // Second loop run with cached results
  for (int i = 0; i < 2; ++i) {
    // Request access to the file.
    auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
        std::vector<uint8_t>, base::ScopedFD>>(nullptr);
    bool allowed;
    base::ScopedFD lifeline_fd;
    base::RunLoop request_file_access_run_loop;
    response->set_return_callback(base::BindOnce(
        [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
           const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
          RequestFileAccessResponse response =
              ParseResponse<RequestFileAccessResponse>(proto_blob);
          *allowed = response.allowed();
          lifeline_fd->reset(dup(fd.get()));
          run_loop->Quit();
        },
        &allowed, &lifeline_fd, &request_file_access_run_loop));
    GetDlpAdaptor()->RequestFileAccess(
        std::move(response), CreateSerializedRequestFileAccessRequest(
                                 {file_path.value()}, kPid, DlpComponent::USB));
    request_file_access_run_loop.Run();

    EXPECT_TRUE(allowed);
    EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

    // Access the file.
    FileOpenRequestResultWaiter waiter;
    helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());
    EXPECT_TRUE(waiter.GetResult());

    // Cancel access to the file.
    lifeline_fd.reset();

    // Let DlpAdaptor process that lifeline_fd is closed.
    base::RunLoop().RunUntilIdle();

    // Second request: still allowed
    FileOpenRequestResultWaiter waiter2;
    helper_.ProcessFileOpenRequest(id, kPid, waiter2.GetCallback());
    EXPECT_TRUE(waiter2.GetResult());
  }
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileSystemRequestedAllowed) {
  InitDatabase();

  // Create files to request access by ids.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  FileMetadata file_metadata;
  file_metadata.set_inode(id.first);
  file_metadata.set_crtime(id.second);
  file_metadata.set_path(file_path.value());
  files_restrictions_.push_back(
      {std::move(file_metadata), RestrictionLevel::LEVEL_BLOCK});
  ON_CALL(*GetMockDlpFilesPolicyServiceProxy(), DoCallMethodWithErrorCallback)
      .WillByDefault(
          Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));
  // Called for both ProcessFileOpen after the closed ScopedFD.
  // RequestFileAccess is allowed with only checking component.
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback)
      .Times(2);

  // Both runs should be answered without getting the cache involved.
  for (int i = 0; i < 2; ++i) {
    // Request access to the file.
    auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
        std::vector<uint8_t>, base::ScopedFD>>(nullptr);
    bool allowed;
    base::ScopedFD lifeline_fd;
    base::RunLoop request_file_access_run_loop;
    response->set_return_callback(base::BindOnce(
        [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
           const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
          RequestFileAccessResponse response =
              ParseResponse<RequestFileAccessResponse>(proto_blob);
          *allowed = response.allowed();
          lifeline_fd->reset(dup(fd.get()));
          run_loop->Quit();
        },
        &allowed, &lifeline_fd, &request_file_access_run_loop));
    GetDlpAdaptor()->RequestFileAccess(
        std::move(response),
        CreateSerializedRequestFileAccessRequest({file_path.value()}, kPid,
                                                 DlpComponent::SYSTEM));
    request_file_access_run_loop.Run();

    EXPECT_TRUE(allowed);
    EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

    // Access the file.
    FileOpenRequestResultWaiter waiter;
    helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());
    EXPECT_TRUE(waiter.GetResult());

    // Cancel access to the file.
    lifeline_fd.reset();

    // Let DlpAdaptor process that lifeline_fd is closed.
    base::RunLoop().RunUntilIdle();

    // Second request: still allowed
    FileOpenRequestResultWaiter waiter2;
    helper_.ProcessFileOpenRequest(id, kPid, waiter2.GetCallback());
    EXPECT_TRUE(waiter2.GetResult());
  }
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedButBadProto) {
  InitDatabase();

  // Create file to request access by ids.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyBadProto));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, DlpComponent::USB));
  request_file_access_run_loop.Run();

  EXPECT_FALSE(allowed);
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedButErrorResponse) {
  InitDatabase();

  // Create file to request access by inodes.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyWithError));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, DlpComponent::USB));
  request_file_access_run_loop.Run();

  EXPECT_FALSE(allowed);
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kRestrictionDetectionError)));
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedCachedNotAllowed) {
  InitDatabase();

  // Create files to request access by ids.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  FileMetadata file_metadata;
  file_metadata.set_inode(id.first);
  file_metadata.set_crtime(id.second);
  file_metadata.set_path(file_path.value());
  files_restrictions_.push_back(
      {std::move(file_metadata), RestrictionLevel::LEVEL_BLOCK});

  // Second loop run with cached results, third with another destination.
  for (int i = 0; i < 3; ++i) {
    // Request access to the file.
    auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
        std::vector<uint8_t>, base::ScopedFD>>(nullptr);
    bool allowed;
    base::ScopedFD lifeline_fd;
    base::RunLoop request_file_access_run_loop;
    response->set_return_callback(base::BindOnce(
        [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
           const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
          RequestFileAccessResponse response =
              ParseResponse<RequestFileAccessResponse>(proto_blob);
          *allowed = response.allowed();
          lifeline_fd->reset(dup(fd.get()));
          run_loop->Quit();
        },
        &allowed, &lifeline_fd, &request_file_access_run_loop));
    // The first and third call needs to query the proxy - the second call is
    // answered from the cache
    if (i == 0 || i == 2) {
      EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
                  DoCallMethodWithErrorCallback(_, _, _, _))
          .WillOnce(
              Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));
    }
    const std::string destination = i < 2 ? "destination" : "destination2";
    GetDlpAdaptor()->RequestFileAccess(
        std::move(response), CreateSerializedRequestFileAccessRequest(
                                 {file_path.value()}, kPid, destination));
    request_file_access_run_loop.Run();

    EXPECT_FALSE(allowed);
    EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));

    is_file_policy_restricted_ = true;
    EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
                DoCallMethodWithErrorCallback(_, _, _, _))
        .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

    // Access the file.
    FileOpenRequestResultWaiter waiter;
    helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());
    EXPECT_FALSE(waiter.GetResult());
  }
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFilesNotAddedAndRequestedAllowed) {
  InitDatabase();

  // Create files to request access by ids.
  base::FilePath file_path1;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1);
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2);
  const FileId id2 = GetFileId(file_path2.value());

  // Add only first file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest(
          {file_path1.value(), file_path2.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);
  EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

  // Access the first file.
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());

  // Access the second file.
  FileOpenRequestResultWaiter waiter2;
  helper_.ProcessFileOpenRequest(id2, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileNotAddedAndImmediatelyAllowed) {
  InitDatabase();

  // Create files to request access by ids.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Access already allowed.
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());
  EXPECT_TRUE(waiter.GetResult());

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(0);

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);

  // Access still allowed.
  FileOpenRequestResultWaiter waiter2;
  helper_.ProcessFileOpenRequest(id, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedNotAllowed) {
  InitDatabase();

  // Create file to request access by id.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  FileMetadata file_metadata;
  file_metadata.set_path(file_path.value());
  file_metadata.set_inode(id.first);
  file_metadata.set_crtime(id.second);
  files_restrictions_.push_back(
      {std::move(file_metadata), RestrictionLevel::LEVEL_BLOCK});
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_FALSE(allowed);
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));

  // Setup callback for DlpFilesPolicyService::IsDlpPolicyMatched()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  // Request access to the file.
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedRequestedAndCancelledNotAllowed) {
  InitDatabase();

  // Create file to request access by id.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  const FileId id = GetFileId(file_path.value());

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::ScopedFD* lifeline_fd, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        lifeline_fd->reset(dup((fd.get())));
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);
  EXPECT_FALSE(IsFdClosed(lifeline_fd.get()));

  // Cancel access to the file.
  lifeline_fd.reset();

  // Let DlpAdaptor process that lifeline_fd is closed.
  base::RunLoop().RunUntilIdle();

  // Setup callback for DlpFilesPolicyService::IsDlpPolicyMatched()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  // Request access to the file.
  FileOpenRequestResultWaiter waiter;
  helper_.ProcessFileOpenRequest(id, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

// DlpAdaptor::RequestFileAccess crashes if file access is requested while the
// database isn't created yet. This test makes sure this doesn't happen anymore.
// https://crbug.com/1267295.
TEST_F(DlpAdaptorTest, RequestAllowedWithoutDatabase) {
  // Create file to request access by id.
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);

  // Request access to the file.
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  bool allowed;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob, const base::ScopedFD& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        run_loop->Quit();
      },
      &allowed, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSources) {
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first, 123456}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), id2.first);
  EXPECT_EQ(file_metadata2.crtime(), id2.second);
  EXPECT_EQ(file_metadata2.source_url(), source2);
  EXPECT_EQ(file_metadata2.referrer_url(), referrer2);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSourcesByPath) {
  InitDatabase();

  // Create files to request sources.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));

  const std::string source1 = "source1";
  const std::string referrer1 = "referrer1";

  // Add one of the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response = GetFilesSources(
      /*ids=*/{}, {file_path1.value(), file_path2.value(), "/bad/path"});

  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.path(), file_path1.value());
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesMixed) {
  InitDatabase();

  // Create files to request sources.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id2.first, 123456}, {file_path1.value(), "/bad/path"});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  // First element - requested by inode.
  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id2.first);
  EXPECT_EQ(file_metadata1.crtime(), id2.second);
  EXPECT_FALSE(file_metadata1.has_path());
  EXPECT_EQ(file_metadata1.source_url(), source2);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer2);

  // Second element - requested by path.
  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), id1.first);
  EXPECT_EQ(file_metadata2.crtime(), id1.second);
  EXPECT_EQ(file_metadata2.path(), file_path1.value());
  EXPECT_EQ(file_metadata2.source_url(), source1);
  EXPECT_EQ(file_metadata2.referrer_url(), referrer1);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesDatabaseMigrated) {
  // Opening the database with the new table.
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add first file to the legacy database.
  helper_.AddFileToLegacyDb(id1, source1, referrer1);

  // Add the second file to the new database.
  AddFilesAndCheck({CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first, 123456}, /*paths=*/{});

  // Only the second file is expected as database was not migrated.
  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata = response.files_metadata()[0];
  EXPECT_EQ(file_metadata.inode(), id2.first);
  EXPECT_EQ(file_metadata.crtime(), id2.second);
  EXPECT_EQ(file_metadata.source_url(), source2);
  EXPECT_EQ(file_metadata.referrer_url(), referrer2);

  // Reinitialize database to run migration. Do it twice to ensure that second
  // migration is not kicked out or doesn't change anything.
  for (int i = 0; i < 2; i++) {
    GetDlpAdaptor()->CloseDatabaseForTesting();
    base::RunLoop run_loop;
    GetDlpAdaptor()->InitDatabase(database_directory_->GetPath(),
                                  run_loop.QuitClosure());
    run_loop.Run();

    response = GetFilesSources({id1.first, id2.first, 123456}, /*paths=*/{});

    ASSERT_EQ(response.files_metadata_size(), 2u);

    FileMetadata file_metadata1 = response.files_metadata()[0];
    EXPECT_EQ(file_metadata1.inode(), id1.first);
    EXPECT_EQ(file_metadata1.crtime(), id1.second);
    EXPECT_EQ(file_metadata1.source_url(), source1);
    EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

    FileMetadata file_metadata2 = response.files_metadata()[1];
    EXPECT_EQ(file_metadata2.inode(), id2.first);
    EXPECT_EQ(file_metadata2.crtime(), id2.second);
    EXPECT_EQ(file_metadata2.source_url(), source2);
    EXPECT_EQ(file_metadata2.referrer_url(), referrer2);
  }
  // There are 4 histogram entries - on the first run when tables were created
  // (false), after data to migrate was added (true), after it was migrated
  // (false) and on the next init indicating that no migration is needed anymore
  // (false).
  EXPECT_THAT(helper_.GetMetrics(kDlpDatabaseMigrationNeededHistogram),
              ElementsAre(static_cast<int>(false), static_cast<int>(true),
                          static_cast<int>(false), static_cast<int>(false)));
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSourcesWithoutDatabase) {
  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database. The addition will be pending, so success
  // is returned.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  EXPECT_EQ(response.files_metadata_size(), 0u);

  // Create database and add pending files.
  InitDatabase();

  // Check that the pending entries were added.
  response = GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), id2.first);
  EXPECT_EQ(file_metadata2.crtime(), id2.second);
  EXPECT_EQ(file_metadata2.source_url(), source2);
  EXPECT_EQ(file_metadata2.referrer_url(), referrer2);
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kDatabaseNotReadyError),
                  static_cast<int>(AdaptorError::kDatabaseNotReadyError)));
}

// TODO(b/290389988): Flaky test
TEST_F(DlpAdaptorTest, DISABLED_GetFilesSourcesWithoutDatabaseNotAdded) {
  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database. The addition will be pending, so success
  // is returned.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, "referrer1"),
                    CreateAddFileRequest(file_path2, source2, "referrer2")},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  EXPECT_EQ(response.files_metadata_size(), 0u);

  helper_.ReCreateAdaptor();

  // Create database and add pending files.
  InitDatabase();

  // Check that the pending entries were not added.
  response = GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  EXPECT_EQ(response.files_metadata_size(), 0u);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedDBReopenedWithCleanup) {
  // Enable feature.
  helper_.SetDatabaseCleanupFeatureEnabled(true);

  // Create database.
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  // Delete one of the files.
  brillo::DeleteFile(file_path2);
  // Reinitialize database.
  GetDlpAdaptor()->CloseDatabaseForTesting();
  base::RunLoop run_loop2;
  GetDlpAdaptor()->InitDatabase(database_directory_->GetPath(),
                                run_loop2.QuitClosure());
  run_loop2.Run();

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedDBReopenedWithoutCleanup) {
  // Disabled feature.
  helper_.SetDatabaseCleanupFeatureEnabled(false);

  // Create database.
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  // Delete one of the files.
  brillo::DeleteFile(file_path2);
  // Reinitialize database.
  GetDlpAdaptor()->CloseDatabaseForTesting();
  base::RunLoop run_loop2;
  GetDlpAdaptor()->InitDatabase(database_directory_->GetPath(),
                                run_loop2.QuitClosure());
  run_loop2.Run();

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), id2.first);
  EXPECT_EQ(file_metadata2.crtime(), id2.second);
  EXPECT_EQ(file_metadata2.source_url(), source2);
  EXPECT_EQ(file_metadata2.referrer_url(), referrer2);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedInFlight) {
  SetRulesAndInitFanotify();
  // Create database.
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  // Delete one of the files.
  brillo::DeleteFile(file_path2);
  // Notify that file was deleted.
  helper_.OnFileDeleted(id2);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

  // Reinitialize database.
  GetDlpAdaptor()->CloseDatabaseForTesting();
  InitDatabase();

  // Delete the other file.
  brillo::DeleteFile(file_path1);
  // Notify that file was deleted.
  helper_.OnFileDeleted(id1);

  response = GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 0u);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSourcesOverwrite) {
  // Create database.
  InitDatabase();

  // Create files to request sources by ids.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source2, referrer2)},
                   /*expected_result=*/true);

  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  GetFilesSourcesResponse response =
      GetFilesSources({id1.first, id2.first}, /*paths=*/{});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), id1.first);
  EXPECT_EQ(file_metadata1.crtime(), id1.second);
  EXPECT_EQ(file_metadata1.source_url(), source1);
  EXPECT_EQ(file_metadata1.referrer_url(), referrer1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), id2.first);
  EXPECT_EQ(file_metadata2.crtime(), id2.second);
  EXPECT_EQ(file_metadata2.source_url(), source2);
  EXPECT_EQ(file_metadata2.referrer_url(), referrer2);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, SetDlpFilesPolicy_EmptyProto) {
  SetDlpFilesPolicyRequest request;
  request.add_rules();
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  std::vector<uint8_t> response_blob =
      GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

  SetDlpFilesPolicyResponse response =
      ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

  EXPECT_FALSE(response.has_error_message());
  EXPECT_FALSE(helper_.IsFanotifyWatcherActive());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, SetDlpFilesPolicy_BadProto) {
  std::vector<uint8_t> response_blob =
      GetDlpAdaptor()->SetDlpFilesPolicy(RandomProtoBlob());

  SetDlpFilesPolicyResponse response =
      ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

  EXPECT_TRUE(response.has_error_message());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, SetDlpFilesPolicy_EnableDisable) {
  // Testing fanotify watcher start for real.
  GetDlpAdaptor()->SetFanotifyWatcherStartedForTesting(false);

  // Send non-empty policy -> enabled.
  {
    SetDlpFilesPolicyRequest request;
    ::dlp::DlpFilesRule* rule = request.add_rules();
    rule->add_source_urls("example.com");
    rule->add_destination_urls("*");
    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    std::vector<uint8_t> response_blob =
        GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

    SetDlpFilesPolicyResponse response =
        ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

    EXPECT_FALSE(response.has_error_message());
    EXPECT_TRUE(helper_.IsFanotifyWatcherActive());
  }

  // Send another non-empty policy -> enabled.
  {
    SetDlpFilesPolicyRequest request;
    ::dlp::DlpFilesRule* rule = request.add_rules();
    rule->add_source_urls("example.com");
    rule->add_destination_urls("google.com");
    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    std::vector<uint8_t> response_blob =
        GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

    SetDlpFilesPolicyResponse response =
        ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

    EXPECT_FALSE(response.has_error_message());
    EXPECT_TRUE(helper_.IsFanotifyWatcherActive());
  }

  // Send same non-empty policy -> enabled.
  {
    SetDlpFilesPolicyRequest request;
    ::dlp::DlpFilesRule* rule = request.add_rules();
    rule->add_source_urls("example.com");
    rule->add_destination_urls("google.com");
    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    std::vector<uint8_t> response_blob =
        GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

    SetDlpFilesPolicyResponse response =
        ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

    EXPECT_FALSE(response.has_error_message());
    EXPECT_TRUE(helper_.IsFanotifyWatcherActive());
  }

  // Send empty policy -> disabled.
  {
    SetDlpFilesPolicyRequest request;
    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());

    std::vector<uint8_t> response_blob =
        GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

    SetDlpFilesPolicyResponse response =
        ParseResponse<SetDlpFilesPolicyResponse>(response_blob);

    EXPECT_FALSE(response.has_error_message());
    EXPECT_FALSE(helper_.IsFanotifyWatcherActive());
  }
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, AddZeroFilesToTheDaemon) {
  AddFilesAndCheck({}, /*expected_result=*/true);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, AddFiles_BadProto) {
  bool success;
  std::unique_ptr<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>
      response = std::make_unique<
          brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
          nullptr);
  base::RunLoop run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* success, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        AddFilesResponse response = ParseResponse<AddFilesResponse>(proto_blob);
        *success = response.error_message().empty();
        run_loop->Quit();
      },
      &success, &run_loop));
  GetDlpAdaptor()->AddFiles(std::move(response), RandomProtoBlob());
  run_loop.Run();
  EXPECT_FALSE(success);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kAddFileError),
                          static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, AddFiles_NonExistentFile) {
  AddFilesAndCheck(
      {CreateAddFileRequest(base::FilePath("/tmp/non-existent-file"), "source",
                            "referrer")},
      /*expected_result=*/false);
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kAddFileError),
                  static_cast<int>(AdaptorError::kInodeRetrievalError)));
}

// TODO(b/304574852): Using TaskEnvironment causes DlpAdaptorTest to hang.
// We need to find another way to fake time or rewrite the test.
TEST_F(DlpAdaptorTest, DISABLED_AddFiles_OldFile) {
  base::FilePath file_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &file_path);
  //  const FileId id = GetFileId(file_path.value());
  ////   Set to file creation time.
  //  task_environment_.FastForwardBy(base::Time::FromTimeT(id.second) -
  //                                  base::Time::Now());
  ////   Advance by a minute.
  //  task_environment_.FastForwardBy(base::Minutes(1));
  AddFilesAndCheck({CreateAddFileRequest(file_path, "source", "referrer")},
                   /*expected_result=*/false);
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kAddFileError),
                          static_cast<int>(AdaptorError::kAddedFileIsTooOld)));
}

TEST_F(DlpAdaptorTest, AddFiles_Symlink) {
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  base::FilePath symlink_path;
  base::CreateTemporaryFileInDir(helper_.home_path(), &symlink_path);
  brillo::DeleteFile(symlink_path);
  base::CreateSymbolicLink(file_path, symlink_path);

  AddFilesAndCheck({CreateAddFileRequest(symlink_path, "source", "referrer")},
                   /*expected_result=*/false);
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kAddFileError),
                  static_cast<int>(AdaptorError::kAddedFileIsNotOnUserHome)));
}

TEST_F(DlpAdaptorTest, RequestFileAccess_BadProto) {
  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  RequestFileAccessResponse request_file_response;
  response->set_return_callback(base::BindOnce(
      [](RequestFileAccessResponse* response, base::ScopedFD* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const base::ScopedFD& fd) {
        *response = ParseResponse<RequestFileAccessResponse>(proto_blob);
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &request_file_response, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(std::move(response), RandomProtoBlob());
  request_file_access_run_loop.Run();

  EXPECT_FALSE(request_file_response.allowed());
  EXPECT_FALSE(request_file_response.error_message().empty());
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, RequestFileAccess_NonExistentFile) {
  InitDatabase();

  auto response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, base::ScopedFD>>(nullptr);
  base::ScopedFD lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  RequestFileAccessResponse request_file_response;
  response->set_return_callback(base::BindOnce(
      [](RequestFileAccessResponse* response, base::ScopedFD* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const base::ScopedFD& fd) {
        *response = ParseResponse<RequestFileAccessResponse>(proto_blob);
        lifeline_fd->reset(dup(fd.get()));
        run_loop->Quit();
      },
      &request_file_response, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response),
      CreateSerializedRequestFileAccessRequest({"/tmp/non-existent-file"}, kPid,
                                               "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(request_file_response.allowed());
  EXPECT_TRUE(request_file_response.error_message().empty());
  EXPECT_TRUE(IsFdClosed(lifeline_fd.get()));
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

TEST_F(DlpAdaptorTest, GetFilesSources_BadProto) {
  GetFilesSourcesResponse result;
  std::unique_ptr<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>
      response = std::make_unique<
          brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
          nullptr);
  base::RunLoop run_loop;
  response->set_return_callback(base::BindOnce(
      [](GetFilesSourcesResponse* result, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        *result = ParseResponse<GetFilesSourcesResponse>(proto_blob);
        run_loop->Quit();
      },
      &result, &run_loop));

  GetDlpAdaptor()->GetFilesSources(std::move(response), RandomProtoBlob());
  run_loop.Run();

  EXPECT_FALSE(result.error_message().empty());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, CheckFilesTransfer_BadProto) {
  InitDatabase();

  CheckFilesTransferResponse result;
  base::RunLoop run_loop;

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
      nullptr);
  response->set_return_callback(base::BindOnce(
      [](CheckFilesTransferResponse* result, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        *result = ParseResponse<CheckFilesTransferResponse>(proto_blob);
        run_loop->Quit();
      },
      &result, &run_loop));
  GetDlpAdaptor()->CheckFilesTransfer(std::move(response), RandomProtoBlob());
  run_loop.Run();

  EXPECT_FALSE(result.error_message().empty());
  EXPECT_TRUE(result.files_paths().empty());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest, CheckFilesTransfer_DbNotInitialized) {
  CheckFilesTransferResponse result;
  base::RunLoop run_loop;

  // Create files.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, "referrer1"),
                    CreateAddFileRequest(file_path2, source2, "referrer2")},
                   /*expected_result=*/true);

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
      nullptr);
  response->set_return_callback(base::BindOnce(
      [](CheckFilesTransferResponse* result, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        *result = ParseResponse<CheckFilesTransferResponse>(proto_blob);
        run_loop->Quit();
      },
      &result, &run_loop));
  GetDlpAdaptor()->CheckFilesTransfer(
      std::move(response),
      CreateSerializedCheckFilesTransferRequest(
          {file_path1.value(), file_path2.value()}, DlpComponent::USB));
  run_loop.Run();

  EXPECT_FALSE(result.error_message().empty());
  EXPECT_TRUE(result.files_paths().empty());
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kDatabaseNotReadyError),
                  static_cast<int>(AdaptorError::kDatabaseNotReadyError)));
}

TEST_F(DlpAdaptorTest, CheckFilesTransfer_IsFilesTransferRestrictedBadProto) {
  // Create database.
  InitDatabase();

  CheckFilesTransferResponse result;
  base::RunLoop run_loop;

  // Create file.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));

  const std::string source1 = "source1";

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, "referrer1")},
                   /*expected_result=*/true);

  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyBadProto));

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
      nullptr);
  response->set_return_callback(base::BindOnce(
      [](CheckFilesTransferResponse* result, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        *result = ParseResponse<CheckFilesTransferResponse>(proto_blob);
        run_loop->Quit();
      },
      &result, &run_loop));
  GetDlpAdaptor()->CheckFilesTransfer(
      std::move(response), CreateSerializedCheckFilesTransferRequest(
                               {file_path1.value()}, DlpComponent::USB));
  run_loop.Run();

  EXPECT_FALSE(result.error_message().empty());
  EXPECT_TRUE(result.files_paths().empty());
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram),
              ElementsAre(static_cast<int>(AdaptorError::kInvalidProtoError)));
}

TEST_F(DlpAdaptorTest,
       CheckFilesTransfer_IsFilesTransferRestrictedResponseError) {
  // Create database.
  InitDatabase();

  CheckFilesTransferResponse result;
  base::RunLoop run_loop;

  // Create file.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));

  const std::string source1 = "source1";

  // Add the file to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, "referrer1")},
                   /*expected_result=*/true);

  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubReplyWithError));

  auto response = std::make_unique<
      brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
      nullptr);
  response->set_return_callback(base::BindOnce(
      [](CheckFilesTransferResponse* result, base::RunLoop* run_loop,
         const std::vector<uint8_t>& proto_blob) {
        *result = ParseResponse<CheckFilesTransferResponse>(proto_blob);
        run_loop->Quit();
      },
      &result, &run_loop));
  GetDlpAdaptor()->CheckFilesTransfer(
      std::move(response), CreateSerializedCheckFilesTransferRequest(
                               {file_path1.value()}, DlpComponent::USB));
  run_loop.Run();

  EXPECT_FALSE(result.error_message().empty());
  EXPECT_TRUE(result.files_paths().empty());
  EXPECT_THAT(
      helper_.GetMetrics(kDlpAdaptorErrorHistogram),
      ElementsAre(static_cast<int>(AdaptorError::kRestrictionDetectionError)));
}

class DlpAdaptorCheckFilesTransferTest
    : public DlpAdaptorTest,
      public ::testing::WithParamInterface<RestrictionLevel> {
 public:
  DlpAdaptorCheckFilesTransferTest(const DlpAdaptorCheckFilesTransferTest&) =
      delete;
  DlpAdaptorCheckFilesTransferTest& operator=(
      const DlpAdaptorCheckFilesTransferTest&) = delete;

 protected:
  DlpAdaptorCheckFilesTransferTest() = default;
  ~DlpAdaptorCheckFilesTransferTest() override = default;
};

INSTANTIATE_TEST_SUITE_P(DlpAdaptor,
                         DlpAdaptorCheckFilesTransferTest,
                         ::testing::Values(RestrictionLevel::LEVEL_UNSPECIFIED,
                                           RestrictionLevel::LEVEL_ALLOW,
                                           RestrictionLevel::LEVEL_REPORT,
                                           RestrictionLevel::LEVEL_WARN_PROCEED,
                                           RestrictionLevel::LEVEL_WARN_CANCEL,
                                           RestrictionLevel::LEVEL_BLOCK));

TEST_P(DlpAdaptorCheckFilesTransferTest, Run) {
  // Create database.
  InitDatabase();

  // Create files.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const FileId id1 = GetFileId(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const FileId id2 = GetFileId(file_path2.value());
  base::FilePath file_path3;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path3));

  const std::string source1 = "source1";
  const std::string source2 = "source2";
  const std::string referrer1 = "referrer1";
  const std::string referrer2 = "referrer2";

  // Add two of the files to the database.
  AddFilesAndCheck({CreateAddFileRequest(file_path1, source1, referrer1),
                    CreateAddFileRequest(file_path2, source2, referrer2)},
                   /*expected_result=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  files_restrictions_.clear();
  FileMetadata file1_metadata;
  file1_metadata.set_inode(id1.first);
  file1_metadata.set_crtime(id1.second);
  file1_metadata.set_path(file_path1.value());
  FileMetadata file2_metadata;
  file2_metadata.set_path(file_path2.value());
  file2_metadata.set_inode(id2.first);
  file2_metadata.set_crtime(id2.second);
  files_restrictions_.push_back(
      {std::move(file1_metadata), RestrictionLevel::LEVEL_BLOCK});
  files_restrictions_.push_back({std::move(file2_metadata), GetParam()});
  const int is_files_transfer_restricted_calls =
      (GetParam() == LEVEL_UNSPECIFIED || GetParam() == LEVEL_WARN_CANCEL) ? 2
                                                                           : 1;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(is_files_transfer_restricted_calls)
      .WillRepeatedly(
          Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Do 2 times for cache.
  for (int i = 0; i < 2; i++) {
    // Request access to the file.
    auto response = std::make_unique<
        brillo::dbus_utils::MockDBusMethodResponse<std::vector<uint8_t>>>(
        nullptr);

    std::vector<std::string> restricted_files_paths;
    base::RunLoop check_files_transfer_run_loop;
    response->set_return_callback(base::BindOnce(
        [](std::vector<std::string>* restricted_files_paths,
           base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob) {
          CheckFilesTransferResponse response =
              ParseResponse<CheckFilesTransferResponse>(proto_blob);
          restricted_files_paths->insert(restricted_files_paths->begin(),
                                         response.files_paths().begin(),
                                         response.files_paths().end());
          run_loop->Quit();
        },
        &restricted_files_paths, &check_files_transfer_run_loop));
    GetDlpAdaptor()->CheckFilesTransfer(
        std::move(response),
        CreateSerializedCheckFilesTransferRequest(
            {file_path1.value(), file_path2.value(), file_path3.value()},
            "destination"));
    check_files_transfer_run_loop.Run();

    if (GetParam() == RestrictionLevel::LEVEL_BLOCK ||
        GetParam() == RestrictionLevel::LEVEL_WARN_CANCEL) {
      EXPECT_EQ(restricted_files_paths.size(), 2u);
    } else {
      EXPECT_EQ(restricted_files_paths.size(), 1u);
      EXPECT_EQ(restricted_files_paths[0], file_path1.value());
    }
  }
  EXPECT_THAT(helper_.GetMetrics(kDlpAdaptorErrorHistogram), ElementsAre());
}

}  // namespace dlp
