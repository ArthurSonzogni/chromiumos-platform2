// Copyright 2021 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "dlp/dlp_adaptor.h"

#include <memory>
#include <poll.h>
#include <string>
#include <utility>

#include <base/callback.h>
#include <base/files/file_util.h>
#include <base/files/scoped_temp_dir.h>
#include <base/memory/scoped_refptr.h>
#include <base/run_loop.h>
#include <brillo/dbus/mock_dbus_method_response.h>
#include <gtest/gtest.h>

#include "dlp/dlp_adaptor_test_helper.h"

using testing::_;
using testing::Invoke;
using testing::Return;

namespace dlp {
namespace {

// Some arbitrary D-Bus message serial number. Required for mocking D-Bus calls.
constexpr int kDBusSerial = 123;
constexpr int kPid = 1234;

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

  std::vector<uint8_t> CreateSerializedAddFileRequest(
      const std::string& file,
      const std::string& source,
      const std::string& referrer) {
    AddFileRequest request;
    request.set_file_path(file);
    request.set_source_url(source);
    request.set_referrer_url(referrer);

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

  std::vector<uint8_t> CreateSerializedCheckFilesTransferRequest(
      std::vector<std::string> files_paths, const std::string& destination) {
    CheckFilesTransferRequest request;
    *request.mutable_files_paths() = {files_paths.begin(), files_paths.end()};
    request.set_destination_url(destination);

    std::vector<uint8_t> proto_blob(request.ByteSizeLong());
    request.SerializeToArray(proto_blob.data(), proto_blob.size());
    return proto_blob;
  }

  std::vector<uint8_t> CreateSerializedGetFilesSourcesRequest(
      std::vector<ino_t> inodes) {
    GetFilesSourcesRequest request;
    *request.mutable_files_inodes() = {inodes.begin(), inodes.end()};

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

  void StubIsFilesTransferRestricted(
      dbus::MethodCall* method_call,
      int /* timeout_ms */,
      dbus::MockObjectProxy::ResponseCallback* response_callback,
      dbus::MockObjectProxy::ErrorCallback* error_callback) {
    method_call->SetSerial(kDBusSerial);
    auto response = dbus::Response::FromMethodCall(method_call);
    dbus::MessageWriter writer(response.get());

    IsFilesTransferRestrictedResponse response_proto;
    *response_proto.mutable_restricted_files() = {restricted_files_.begin(),
                                                  restricted_files_.end()};

    writer.AppendProtoAsArrayOfBytes(response_proto);
    std::move(*response_callback).Run(response.get());
  }

  void AddFileAndCheck(const base::FilePath& file_path,
                       const std::string& source,
                       const std::string& referrer,
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
          AddFileResponse response = ParseResponse<AddFileResponse>(proto_blob);
          *success = response.error_message().empty();
          run_loop->Quit();
        },
        &success, &run_loop));
    GetDlpAdaptor()->AddFile(
        std::move(response),
        CreateSerializedAddFileRequest(file_path.value(), source, referrer));
    run_loop.Run();
    EXPECT_EQ(expected_result, success);
  }

  GetFilesSourcesResponse GetFilesSources(std::vector<ino_t> inodes) {
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
        std::move(response), CreateSerializedGetFilesSourcesRequest(inodes));
    run_loop.Run();
    return result;
  }

 protected:
  bool is_file_policy_restricted_;
  std::vector<FileMetadata> restricted_files_;

  DlpAdaptorTestHelper helper_;
};

TEST_F(DlpAdaptorTest, AllowedWithoutDatabase) {
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(
      /*inode=*/1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, AllowedWithDatabase) {
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(
      /*inode=*/1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, NotRestrictedFileAddedAndAllowed) {
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  AddFileAndCheck(file_path, "source", "referrer", /*success=*/true);

  ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  is_file_policy_restricted_ = false;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedAndNotAllowed) {
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  AddFileAndCheck(file_path, "source", "referrer", /*success=*/true);

  ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
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

  // Create files to request access by inodes.
  base::FilePath file_path1;
  base::CreateTemporaryFile(&file_path1);
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  base::CreateTemporaryFile(&file_path2);
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  // Add the files to the database.
  AddFileAndCheck(file_path1, "source", "referrer", /*success=*/true);
  AddFileAndCheck(file_path2, "source", "referrer", /*success=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
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
  GetDlpAdaptor()->ProcessFileOpenRequest(inode1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());

  // Second request still allowed.
  FileOpenRequestResultWaiter waiter2;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode1, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());

  // Access the second file.
  FileOpenRequestResultWaiter waiter3;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode2, kPid, waiter3.GetCallback());

  EXPECT_TRUE(waiter3.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFilesNotAddedAndRequestedAllowed) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request access by inodes.
  base::FilePath file_path1;
  base::CreateTemporaryFile(&file_path1);
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  base::CreateTemporaryFile(&file_path2);
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  // Add only first file to the database.
  AddFileAndCheck(file_path1, "source", "referrer", /*success=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
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
  GetDlpAdaptor()->ProcessFileOpenRequest(inode1, kPid, waiter.GetCallback());

  EXPECT_TRUE(waiter.GetResult());

  // Access the second file.
  FileOpenRequestResultWaiter waiter2;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode2, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFileNotAddedAndImmediatelyAllowed) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request access by inodes.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Access already allowed.
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());
  EXPECT_TRUE(waiter.GetResult());

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .Times(0);

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
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
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter2.GetCallback());

  EXPECT_TRUE(waiter2.GetResult());
}
//
TEST_F(DlpAdaptorTest, RestrictedFileAddedAndRequestedNotAllowed) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Add the file to the database.
  AddFileAndCheck(file_path, "source", "referrer", /*success=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  FileMetadata file_metadata;
  file_metadata.set_path(file_path.value());
  restricted_files_.push_back(std::move(file_metadata));
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
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
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
}

TEST_F(DlpAdaptorTest, RestrictedFileAddedRequestedAndCancelledNotAllowed) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);
  const ino_t inode = GetDlpAdaptor()->GetInodeValue(file_path.value());

  // Add the file to the database.
  AddFileAndCheck(file_path, "source", "referrer", /*success=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        *lifeline_fd = brillo::dbus_utils::FileDescriptor(fd.get());
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
  IGNORE_EINTR(close(lifeline_fd.release()));

  // Let DlpAdaptor process that lifeline_fd is closed.
  base::RunLoop().RunUntilIdle();

  // Setup callback for DlpFilesPolicyService::IsDlpPolicyMatched()
  is_file_policy_restricted_ = true;
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsDlpPolicyMatched));

  // Request access to the file.
  FileOpenRequestResultWaiter waiter;
  GetDlpAdaptor()->ProcessFileOpenRequest(inode, kPid, waiter.GetCallback());

  EXPECT_FALSE(waiter.GetResult());
}

// DlpAdaptor::RequestFileAccess crashes if file access is requested while the
// database isn't created yet. This test makes sure this doesn't happen anymore.
// https://crbug.com/1267295.
TEST_F(DlpAdaptorTest, RequestAllowedWithoutDatabase) {
  // Create file to request access by inode.
  base::FilePath file_path;
  base::CreateTemporaryFile(&file_path);

  // Request access to the file.
  std::unique_ptr<brillo::dbus_utils::MockDBusMethodResponse<
      std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>
      response = std::make_unique<brillo::dbus_utils::MockDBusMethodResponse<
          std::vector<uint8_t>, brillo::dbus_utils::FileDescriptor>>(nullptr);
  bool allowed;
  brillo::dbus_utils::FileDescriptor lifeline_fd;
  base::RunLoop request_file_access_run_loop;
  response->set_return_callback(base::BindOnce(
      [](bool* allowed, brillo::dbus_utils::FileDescriptor* lifeline_fd,
         base::RunLoop* run_loop, const std::vector<uint8_t>& proto_blob,
         const brillo::dbus_utils::FileDescriptor& fd) {
        RequestFileAccessResponse response =
            ParseResponse<RequestFileAccessResponse>(proto_blob);
        *allowed = response.allowed();
        run_loop->Quit();
      },
      &allowed, &lifeline_fd, &request_file_access_run_loop));
  GetDlpAdaptor()->RequestFileAccess(
      std::move(response), CreateSerializedRequestFileAccessRequest(
                               {file_path.value()}, kPid, "destination"));
  request_file_access_run_loop.Run();

  EXPECT_TRUE(allowed);
}

TEST_F(DlpAdaptorTest, GetFilesSources) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/true);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/true);

  GetFilesSourcesResponse response = GetFilesSources({inode1, inode2, 123456});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), inode1);
  EXPECT_EQ(file_metadata1.source_url(), source1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), inode2);
  EXPECT_EQ(file_metadata2.source_url(), source2);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesWithoutDatabase) {
  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/false);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/false);

  GetFilesSourcesResponse response = GetFilesSources({inode1, inode2});

  EXPECT_EQ(response.files_metadata_size(), 0u);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedDBReopenedWithCleanup) {
  // Enable feature.
  helper_.SetDatabaseCleanupFeatureEnabled(true);

  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/true);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/true);

  // Delete one of the files.
  base::DeleteFile(file_path2);
  // Reinitialize database.
  GetDlpAdaptor()->CloseDatabaseForTesting();
  base::RunLoop run_loop2;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop2.QuitClosure());
  run_loop2.Run();

  GetFilesSourcesResponse response = GetFilesSources({inode1, inode2});

  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), inode1);
  EXPECT_EQ(file_metadata1.source_url(), source1);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedDBReopenedWithoutCleanup) {
  // Disabled feature.
  helper_.SetDatabaseCleanupFeatureEnabled(false);

  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/true);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/true);

  // Delete one of the files.
  base::DeleteFile(file_path2);
  // Reinitialize database.
  GetDlpAdaptor()->CloseDatabaseForTesting();
  base::RunLoop run_loop2;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop2.QuitClosure());
  run_loop2.Run();

  GetFilesSourcesResponse response = GetFilesSources({inode1, inode2});

  ASSERT_EQ(response.files_metadata_size(), 2u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), inode1);
  EXPECT_EQ(file_metadata1.source_url(), source1);

  FileMetadata file_metadata2 = response.files_metadata()[1];
  EXPECT_EQ(file_metadata2.inode(), inode2);
  EXPECT_EQ(file_metadata2.source_url(), source2);
}

TEST_F(DlpAdaptorTest, GetFilesSourcesFileDeletedInFlight) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files to request sources by inodes.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path1));
  const ino_t inode1 = GetDlpAdaptor()->GetInodeValue(file_path1.value());
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFileInDir(helper_.home_path(), &file_path2));
  const ino_t inode2 = GetDlpAdaptor()->GetInodeValue(file_path2.value());

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/true);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/true);

  // Delete one of the files.
  base::DeleteFile(file_path2);
  // Notify that file was deleted.
  GetDlpAdaptor()->OnFileDeleted(inode2);

  GetFilesSourcesResponse response = GetFilesSources({inode1, inode2});

  ASSERT_EQ(response.files_metadata_size(), 1u);

  FileMetadata file_metadata1 = response.files_metadata()[0];
  EXPECT_EQ(file_metadata1.inode(), inode1);
  EXPECT_EQ(file_metadata1.source_url(), source1);
}

TEST_F(DlpAdaptorTest, SetDlpFilesPolicy) {
  SetDlpFilesPolicyRequest request;
  request.add_rules();
  std::vector<uint8_t> proto_blob(request.ByteSizeLong());
  request.SerializeToArray(proto_blob.data(), proto_blob.size());

  std::vector<uint8_t> response_blob =
      GetDlpAdaptor()->SetDlpFilesPolicy(proto_blob);

  RequestFileAccessResponse response =
      ParseResponse<RequestFileAccessResponse>(response_blob);

  EXPECT_FALSE(response.has_error_message());
}

TEST_F(DlpAdaptorTest, CheckFilesTransfer) {
  // Create database.
  base::ScopedTempDir database_directory;
  ASSERT_TRUE(database_directory.CreateUniqueTempDir());
  base::RunLoop run_loop;
  GetDlpAdaptor()->InitDatabase(database_directory.GetPath(),
                                run_loop.QuitClosure());
  run_loop.Run();

  // Create files.
  base::FilePath file_path1;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path1));
  base::FilePath file_path2;
  ASSERT_TRUE(base::CreateTemporaryFile(&file_path2));

  const std::string source1 = "source1";
  const std::string source2 = "source2";

  // Add the files to the database.
  AddFileAndCheck(file_path1, source1, "referrer1", /*success=*/true);
  AddFileAndCheck(file_path2, source2, "referrer2", /*success=*/true);

  // Setup callback for DlpFilesPolicyService::IsFilesTransferRestricted()
  restricted_files_.clear();
  FileMetadata file1_metadata;
  file1_metadata.set_path(file_path1.value());
  restricted_files_.push_back(std::move(file1_metadata));
  EXPECT_CALL(*GetMockDlpFilesPolicyServiceProxy(),
              DoCallMethodWithErrorCallback(_, _, _, _))
      .WillOnce(Invoke(this, &DlpAdaptorTest::StubIsFilesTransferRestricted));

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
          {file_path1.value(), file_path2.value()}, "destination"));
  check_files_transfer_run_loop.Run();

  EXPECT_EQ(restricted_files_paths.size(), 1u);
}

}  // namespace dlp
