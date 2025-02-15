// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ash/policy/dlp/dlp_files_controller_ash.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <memory>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "base/check.h"
#include "base/files/file.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "base/functional/bind.h"
#include "base/functional/callback.h"
#include "base/functional/callback_forward.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/scoped_refptr.h"
#include "base/run_loop.h"
#include "base/strings/strcat.h"
#include "base/strings/utf_string_conversions.h"
#include "base/task/sequenced_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/metrics/histogram_tester.h"
#include "base/test/mock_callback.h"
#include "base/test/repeating_test_future.h"
#include "base/test/test_future.h"
#include "base/test/test_mock_time_task_runner.h"
#include "base/time/time.h"
#include "chrome/browser/apps/app_service/app_service_proxy.h"
#include "chrome/browser/apps/app_service/app_service_proxy_factory.h"
#include "chrome/browser/apps/app_service/app_service_test.h"
#include "chrome/browser/ash/crostini/crostini_manager.h"
#include "chrome/browser/ash/crostini/fake_crostini_features.h"
#include "chrome/browser/ash/drive/drive_integration_service.h"
#include "chrome/browser/ash/file_manager/fileapi_util.h"
#include "chrome/browser/ash/file_manager/path_util.h"
#include "chrome/browser/ash/fileapi/file_system_backend.h"
#include "chrome/browser/ash/login/users/fake_chrome_user_manager.h"
#include "chrome/browser/ash/policy/dlp/dlp_files_event_storage.h"
#include "chrome/browser/ash/policy/dlp/files_policy_notification_manager.h"
#include "chrome/browser/ash/policy/dlp/files_policy_notification_manager_factory.h"
#include "chrome/browser/ash/policy/dlp/mock_files_policy_notification_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_file_destination.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_histogram_helper.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_policy_event.pb.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_reporting_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_reporting_manager_test_helper.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager.h"
#include "chrome/browser/chromeos/policy/dlp/dlp_rules_manager_factory.h"
#include "chrome/browser/chromeos/policy/dlp/mock_dlp_rules_manager.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/test/base/testing_profile.h"
#include "chromeos/ash/components/dbus/chunneld/chunneld_client.h"
#include "chromeos/ash/components/dbus/cicerone/cicerone_client.h"
#include "chromeos/ash/components/dbus/concierge/concierge_client.h"
#include "chromeos/ash/components/dbus/seneschal/seneschal_client.h"
#include "chromeos/dbus/dlp/dlp_client.h"
#include "chromeos/dbus/dlp/dlp_service.pb.h"
#include "chromeos/ui/base/file_icon_util.h"
#include "components/drive/drive_pref_names.h"
#include "components/file_access/scoped_file_access.h"
#include "components/reporting/util/test_util.h"
#include "components/services/app_service/public/cpp/app_types.h"
#include "components/services/app_service/public/cpp/intent_util.h"
#include "components/user_manager/scoped_user_manager.h"
#include "content/public/test/browser_task_environment.h"
#include "extensions/common/constants.h"
#include "storage/browser/file_system/external_mount_points.h"
#include "storage/browser/file_system/file_system_url.h"
#include "storage/browser/quota/quota_manager_proxy.h"
#include "storage/browser/test/test_file_system_context.h"
#include "storage/common/file_system/file_system_types.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

using blink::mojom::FileSystemFileInfo;
using blink::mojom::NativeFileInfo;
using storage::FileSystemURL;
using testing::_;
using testing::Mock;

namespace policy {

namespace {

constexpr char kEmailId[] = "test@example.com";
constexpr char kGaiaId[] = "12345";

constexpr char kExampleUrl1[] = "https://example1.com/";
constexpr char kExampleUrl2[] = "https://example2.com/";
constexpr char kExampleUrl3[] = "https://example3.com/";
constexpr char kExampleUrl4[] = "https://example4.com/";
constexpr char kExampleUrl5[] = "https://example5.com/";

constexpr char kExampleSourcePattern1[] = "example1.com";
constexpr char kExampleSourcePattern2[] = "example2.com";
constexpr char kExampleSourcePattern3[] = "example3.com";
constexpr char kExampleSourcePattern4[] = "example4.com";

constexpr ino_t kInode1 = 1;
constexpr ino_t kInode2 = 2;
constexpr ino_t kInode3 = 3;
constexpr ino_t kInode4 = 4;
constexpr ino_t kInode5 = 5;

constexpr char kFilePath1[] = "test1.txt";
constexpr char kFilePath2[] = "test2.txt";
constexpr char kFilePath3[] = "test3.txt";
constexpr char kFilePath4[] = "test4.txt";
constexpr char kFilePath5[] = "test5.txt";

constexpr char kChromeAppId[] = "chromeApp";
constexpr char kArcAppId[] = "arcApp";
constexpr char kCrostiniAppId[] = "crostiniApp";
constexpr char kPluginVmAppId[] = "pluginVmApp";
constexpr char kWebAppId[] = "webApp";

constexpr char kRuleName1[] = "rule #1";
constexpr char kRuleName2[] = "rule #2";
constexpr char kRuleName3[] = "rule #3";
constexpr char kRuleName4[] = "rule #4";
constexpr char kRuleId1[] = "testid1";
constexpr char kRuleId2[] = "testid2";
constexpr char kRuleId3[] = "testid3";
constexpr char kRuleId4[] = "testid4";
const DlpRulesManager::RuleMetadata kRuleMetadata1(kRuleName1, kRuleId1);
const DlpRulesManager::RuleMetadata kRuleMetadata2(kRuleName2, kRuleId2);
const DlpRulesManager::RuleMetadata kRuleMetadata3(kRuleName3, kRuleId3);
const DlpRulesManager::RuleMetadata kRuleMetadata4(kRuleName4, kRuleId4);

bool CreateDummyFile(const base::FilePath& path) {
  return WriteFile(path, "42", sizeof("42")) == sizeof("42");
}

// For a given |root| converts the given virtual |path| to a GURL.
GURL ToGURL(const base::FilePath& root, const std::string& path) {
  const std::string abs_path = root.Append(path).value();
  return GURL(base::StrCat({url::kFileSystemScheme, ":",
                            file_manager::util::GetFilesAppOrigin().Serialize(),
                            abs_path}));
}

struct FilesTransferInfo {
  FilesTransferInfo(policy::dlp::FileAction files_action,
                    std::vector<ino_t> file_inodes,
                    std::vector<std::string> file_sources,
                    std::vector<std::string> file_paths)
      : files_action(files_action),
        file_inodes(file_inodes),
        file_sources(file_sources),
        file_paths(file_paths) {}

  policy::dlp::FileAction files_action;
  std::vector<ino_t> file_inodes;
  std::vector<std::string> file_sources;
  std::vector<std::string> file_paths;
};

using MockIsFilesTransferRestrictedCallback =
    testing::StrictMock<base::MockCallback<
        DlpFilesControllerAsh::IsFilesTransferRestrictedCallback>>;
using MockCheckIfDlpAllowedCallback = testing::StrictMock<
    base::MockCallback<DlpFilesControllerAsh::CheckIfDlpAllowedCallback>>;
using MockGetFilesSources =
    testing::StrictMock<base::MockCallback<base::RepeatingCallback<void(
        ::dlp::GetFilesSourcesRequest,
        ::chromeos::DlpClient::GetFilesSourcesCallback)>>>;
using MockAddFiles = testing::StrictMock<base::MockCallback<
    base::RepeatingCallback<void(::dlp::AddFilesRequest,
                                 ::chromeos::DlpClient::AddFilesCallback)>>>;
using FileDaemonInfo = policy::DlpFilesControllerAsh::FileDaemonInfo;

}  // namespace

class DlpFilesControllerAshTest : public testing::Test {
 public:
  DlpFilesControllerAshTest(const DlpFilesControllerAshTest&) = delete;
  DlpFilesControllerAshTest& operator=(const DlpFilesControllerAshTest&) =
      delete;

 protected:
  DlpFilesControllerAshTest()
      : profile_(std::make_unique<TestingProfile>()),
        user_manager_(new ash::FakeChromeUserManager()),
        scoped_user_manager_(std::make_unique<user_manager::ScopedUserManager>(
            base::WrapUnique(user_manager_.get()))) {}

  ~DlpFilesControllerAshTest() override = default;

  void SetUp() override {
    AccountId account_id = AccountId::FromUserEmailGaiaId(kEmailId, kGaiaId);
    profile_->SetIsNewProfile(true);
    user_manager::User* user =
        user_manager_->AddUserWithAffiliationAndTypeAndProfile(
            account_id, /*is_affiliated=*/false,
            user_manager::USER_TYPE_REGULAR, profile_.get());
    user_manager_->UserLoggedIn(account_id, user->username_hash(),
                                /*browser_restart=*/false,
                                /*is_child=*/false);
    user_manager_->SimulateUserProfileLoad(account_id);

    policy::DlpRulesManagerFactory::GetInstance()->SetTestingFactory(
        profile_.get(),
        base::BindRepeating(&DlpFilesControllerAshTest::SetDlpRulesManager,
                            base::Unretained(this)));
    ASSERT_TRUE(policy::DlpRulesManagerFactory::GetForPrimaryProfile());
    ASSERT_TRUE(rules_manager_);

    // Set FilesPolicyNotificationManager.
    policy::FilesPolicyNotificationManagerFactory::GetInstance()
        ->SetTestingFactory(
            profile_.get(),
            base::BindRepeating(
                &DlpFilesControllerAshTest::SetFilesPolicyNotificationManager,
                base::Unretained(this)));

    ASSERT_TRUE(
        policy::FilesPolicyNotificationManagerFactory::GetForBrowserContext(
            profile_.get()));
    ASSERT_TRUE(fpnm_);

    chromeos::DlpClient::InitializeFake();
    chromeos::DlpClient::Get()->GetTestInterface()->SetIsAlive(true);

    my_files_dir_ =
        file_manager::util::GetMyFilesFolderForProfile(profile_.get());
    ASSERT_TRUE(base::CreateDirectory(my_files_dir_));
    file_system_context_ =
        storage::CreateFileSystemContextForTesting(nullptr, my_files_dir_);
    my_files_dir_url_ = CreateFileSystemURL(my_files_dir_.value());

    ASSERT_TRUE(files_controller_);
    files_controller_->SetFileSystemContextForTesting(
        file_system_context_.get());
  }

  void TearDown() override {
    scoped_user_manager_.reset();
    profile_.reset();
    reporting_manager_.reset();

    if (chromeos::DlpClient::Get()) {
      chromeos::DlpClient::Shutdown();
    }
  }

  std::unique_ptr<KeyedService> SetDlpRulesManager(
      content::BrowserContext* context) {
    auto dlp_rules_manager = std::make_unique<MockDlpRulesManager>();
    rules_manager_ = dlp_rules_manager.get();

    files_controller_ =
        std::make_unique<DlpFilesControllerAsh>(*rules_manager_);

    event_storage_ = files_controller_->GetEventStorageForTesting();
    DCHECK(event_storage_);

    task_runner_ = base::MakeRefCounted<base::TestMockTimeTaskRunner>();
    event_storage_->SetTaskRunnerForTesting(task_runner_);

    reporting_manager_ = std::make_unique<DlpReportingManager>();
    SetReportQueueForReportingManager(
        reporting_manager_.get(), events,
        base::SequencedTaskRunner::GetCurrentDefault());
    ON_CALL(*rules_manager_, GetReportingManager)
        .WillByDefault(::testing::Return(reporting_manager_.get()));

    return dlp_rules_manager;
  }

  std::unique_ptr<KeyedService> SetFilesPolicyNotificationManager(
      content::BrowserContext* context) {
    auto fpnm = std::make_unique<
        testing::StrictMock<MockFilesPolicyNotificationManager>>(
        profile_.get());
    fpnm_ = fpnm.get();

    return fpnm;
  }

  storage::FileSystemURL CreateFileSystemURL(const std::string& path) {
    return storage::FileSystemURL::CreateForTest(
        kTestStorageKey, storage::kFileSystemTypeLocal,
        base::FilePath::FromUTF8Unsafe(path));
  }

  void AddFilesToDlpClient(std::vector<FileDaemonInfo> files,
                           std::vector<FileSystemURL>& out_files_urls) {
    ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());

    base::MockCallback<chromeos::DlpClient::AddFilesCallback> add_files_cb;
    EXPECT_CALL(add_files_cb, Run(testing::_)).Times(1);

    ::dlp::AddFilesRequest request;
    for (const auto& file : files) {
      ASSERT_TRUE(CreateDummyFile(file.path));
      ::dlp::AddFileRequest* add_file_req = request.add_add_file_requests();
      add_file_req->set_file_path(file.path.value());
      add_file_req->set_source_url(file.source_url.spec());

      auto file_url = CreateFileSystemURL(file.path.value());
      ASSERT_TRUE(file_url.is_valid());
      out_files_urls.push_back(std::move(file_url));
    }
    chromeos::DlpClient::Get()->AddFiles(request, add_files_cb.Get());
    testing::Mock::VerifyAndClearExpectations(&add_files_cb);
  }

  content::BrowserTaskEnvironment task_environment_;

  std::unique_ptr<TestingProfile> profile_;
  raw_ptr<ash::FakeChromeUserManager, ExperimentalAsh> user_manager_;
  std::unique_ptr<user_manager::ScopedUserManager> scoped_user_manager_;

  raw_ptr<MockDlpRulesManager, ExperimentalAsh> rules_manager_ = nullptr;
  raw_ptr<MockFilesPolicyNotificationManager, ExperimentalAsh> fpnm_ = nullptr;
  std::unique_ptr<DlpFilesControllerAsh> files_controller_;
  std::unique_ptr<DlpReportingManager> reporting_manager_;
  std::vector<DlpPolicyEvent> events;
  raw_ptr<DlpFilesEventStorage, ExperimentalAsh> event_storage_ = nullptr;
  scoped_refptr<base::TestMockTimeTaskRunner> task_runner_;

  scoped_refptr<storage::FileSystemContext> file_system_context_;

  const blink::StorageKey kTestStorageKey =
      blink::StorageKey::CreateFromStringForTesting("https://example.com/test");
  base::FilePath my_files_dir_;
  FileSystemURL my_files_dir_url_;
};

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_DiffFileSystem) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<FileSystemURL> transferred_files(
      {files_urls[0], files_urls[1], files_urls[2]});
  std::vector<FileSystemURL> disallowed_files({files_urls[0], files_urls[2]});

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  for (const auto& file : disallowed_files) {
    check_files_transfer_response.add_files_paths(file.path().value());
  }
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  mount_points->RegisterFileSystem(
      ash::kSystemMountNameArchive, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kArchiveMountPath));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "archive",
      base::FilePath("file.rar/path/in/archive"));

  DlpFilesController::SetNewFilesPolicyUXEnabledForTesting(/*is_enabled=*/true);
  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{1},
                          std::vector<base::FilePath>{files_urls[0].path(),
                                                      files_urls[2].path()},
                          dlp::FileAction::kMove));

  base::test::TestFuture<std::vector<FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(/*task_id=*/1, transferred_files,
                                            dst_url, /*is_move=*/true,
                                            future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(disallowed_files, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(dst_url.path().value(), request.destination_url());
  EXPECT_EQ(::dlp::FileAction::MOVE, request.file_action());
  EXPECT_EQ(1u, request.io_task_id());
}

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_SameFileSystem) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> transferred_files(
      {files_urls[0], files_urls[1], files_urls[2]});

  base::test::TestFuture<std::vector<storage::FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(
      /*task_id=*/absl::nullopt, transferred_files,
      CreateFileSystemURL("Downloads"),
      /*is_move=*/false, future.GetCallback());
  EXPECT_EQ(0u, future.Get().size());
}

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_ClientNotRunning) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> transferred_files(
      {files_urls[0], files_urls[1], files_urls[2]});

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  mount_points->RegisterFileSystem(
      ash::kSystemMountNameArchive, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kArchiveMountPath));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "archive",
      base::FilePath("file.rar/path/in/archive"));

  chromeos::DlpClient::Get()->GetTestInterface()->SetIsAlive(false);
  base::test::TestFuture<std::vector<storage::FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(
      /*task_id=*/absl::nullopt, transferred_files, dst_url, /*is_move=*/true,
      future.GetCallback());
  EXPECT_EQ(0u, future.Get().size());
}

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_ErrorResponse) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> transferred_files(
      {files_urls[0], files_urls[1], files_urls[2]});

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  mount_points->RegisterFileSystem(
      ash::kSystemMountNameArchive, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kArchiveMountPath));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "archive",
      base::FilePath("file.rar/path/in/archive"));

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(files_urls[0].path().value());
  check_files_transfer_response.add_files_paths(files_urls[2].path().value());
  check_files_transfer_response.set_error_message("Did not receive a reply.");
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  base::test::TestFuture<std::vector<storage::FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(
      /*task_id=*/absl::nullopt, transferred_files, dst_url,
      /*is_move=*/false, future.GetCallback());

  ASSERT_EQ(3u, future.Get().size());
  EXPECT_EQ(transferred_files, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(dst_url.path().value(), request.destination_url());
  EXPECT_EQ(::dlp::FileAction::COPY, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_MultiFolder) {
  base::ScopedTempDir sub_dir1;
  ASSERT_TRUE(sub_dir1.CreateUniqueTempDirUnderPath(my_files_dir_));
  base::ScopedTempDir sub_dir2;
  ASSERT_TRUE(sub_dir2.CreateUniqueTempDirUnderPath(sub_dir1.GetPath()));
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, sub_dir1.GetPath().AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, sub_dir1.GetPath().AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, sub_dir1.GetPath().AppendASCII(kFilePath3),
                     kExampleUrl3),
      FileDaemonInfo(kInode4, sub_dir2.GetPath().AppendASCII(kFilePath4),
                     kExampleUrl4),
      FileDaemonInfo(kInode5, sub_dir2.GetPath().AppendASCII(kFilePath5),
                     kExampleUrl5)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> transferred_files(
      {CreateFileSystemURL(sub_dir1.GetPath().value())});

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  mount_points->RegisterFileSystem(
      ash::kSystemMountNameArchive, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kArchiveMountPath));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "archive",
      base::FilePath("file.rar/path/in/archive"));

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(files_urls[1].path().value());
  check_files_transfer_response.add_files_paths(files_urls[2].path().value());
  check_files_transfer_response.add_files_paths(files_urls[4].path().value());
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  DlpFilesController::SetNewFilesPolicyUXEnabledForTesting(/*is_enabled=*/true);
  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{1},
                          std::vector<base::FilePath>{files_urls[1].path(),
                                                      files_urls[2].path(),
                                                      files_urls[4].path()},
                          dlp::FileAction::kCopy));

  base::test::TestFuture<std::vector<storage::FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(/*task_id=*/1, transferred_files,
                                            dst_url, /*is_move=*/false,
                                            future.GetCallback());

  std::vector<storage::FileSystemURL> expected_restricted_files(
      {files_urls[1], files_urls[2], files_urls[4]});
  ASSERT_EQ(3u, future.Get().size());
  EXPECT_EQ(expected_restricted_files, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(dst_url.path().value(), request.destination_url());
  EXPECT_EQ(::dlp::FileAction::COPY, request.file_action());
  EXPECT_EQ(1u, request.io_task_id());
}

TEST_F(DlpFilesControllerAshTest, CheckIfTransferAllowed_ExternalFiles) {
  base::ScopedTempDir external_dir;
  ASSERT_TRUE(external_dir.CreateUniqueTempDir());
  base::FilePath file_path1 = external_dir.GetPath().AppendASCII(kFilePath1);
  ASSERT_TRUE(CreateDummyFile(file_path1));
  auto file_url1 = CreateFileSystemURL(file_path1.value());
  base::FilePath file_path2 = external_dir.GetPath().AppendASCII(kFilePath2);
  ASSERT_TRUE(CreateDummyFile(file_path2));
  auto file_url2 = CreateFileSystemURL(file_path2.value());

  std::vector<FileSystemURL> transferred_files({file_url1, file_url2});
  base::test::TestFuture<std::vector<FileSystemURL>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfTransferAllowed(
      /*task_id=*/absl::nullopt, transferred_files, my_files_dir_url_,
      /*is_move=*/true, future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(std::vector<FileSystemURL>(), future.Take());

  // Validate that no files were requested from the daemon.
  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  ASSERT_EQ(request.files_paths().size(), 0);
}

TEST_F(DlpFilesControllerAshTest, FilterDisallowedUploads_EmptyList) {
  base::test::TestFuture<std::vector<ui::SelectedFileInfo>> future;

  ASSERT_TRUE(files_controller_);
  files_controller_->FilterDisallowedUploads(
      {}, DlpFileDestination(kExampleUrl1), future.GetCallback());

  std::vector<ui::SelectedFileInfo> filtered_uploads;

  ASSERT_EQ(0u, future.Get().size());
}

TEST_F(DlpFilesControllerAshTest, FilterDisallowedUploads_MixedFiles) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<ui::SelectedFileInfo> uploaded_files;
  uploaded_files.emplace_back(files_urls[0].path(), files_urls[0].path());
  uploaded_files.emplace_back(files_urls[1].path(), files_urls[1].path());
  uploaded_files.emplace_back(files_urls[2].path(), files_urls[2].path());

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(files_urls[0].path().value());
  check_files_transfer_response.add_files_paths(files_urls[2].path().value());
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{absl::nullopt},
                          std::vector<base::FilePath>{files_urls[0].path(),
                                                      files_urls[2].path()},
                          dlp::FileAction::kUpload));

  base::test::TestFuture<std::vector<ui::SelectedFileInfo>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->FilterDisallowedUploads(std::move(uploaded_files),
                                             DlpFileDestination(kExampleUrl1),
                                             future.GetCallback());

  std::vector<ui::SelectedFileInfo> filtered_uploads;
  filtered_uploads.emplace_back(files_urls[1].path(), files_urls[1].path());

  ASSERT_EQ(1u, future.Get().size());
  EXPECT_EQ(filtered_uploads, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(kExampleUrl1, request.destination_url());
  EXPECT_EQ(::dlp::FileAction::UPLOAD, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

TEST_F(DlpFilesControllerAshTest, FilterDisallowedUploads_ErrorResponse) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<ui::SelectedFileInfo> selected_files;
  selected_files.emplace_back(files_urls[0].path(), files_urls[0].path());
  selected_files.emplace_back(files_urls[1].path(), files_urls[1].path());
  selected_files.emplace_back(files_urls[2].path(), files_urls[2].path());

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(files_urls[0].path().value());
  check_files_transfer_response.add_files_paths(files_urls[2].path().value());
  check_files_transfer_response.set_error_message("Did not receive a reply.");
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  base::test::TestFuture<std::vector<ui::SelectedFileInfo>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->FilterDisallowedUploads(
      selected_files, DlpFileDestination(kExampleUrl1), future.GetCallback());

  ASSERT_EQ(3u, future.Get().size());
  EXPECT_EQ(selected_files, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(kExampleUrl1, request.destination_url());
  EXPECT_EQ(::dlp::FileAction::UPLOAD, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

TEST_F(DlpFilesControllerAshTest, FilterDisallowedUploads_MultiFolder) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  base::ScopedTempDir sub_dir1;
  ASSERT_TRUE(sub_dir1.CreateUniqueTempDirUnderPath(my_files_dir_));
  base::ScopedTempDir sub_dir2;
  ASSERT_TRUE(sub_dir2.CreateUniqueTempDirUnderPath(my_files_dir_));
  base::ScopedTempDir sub_dir2_1;
  ASSERT_TRUE(sub_dir2_1.CreateUniqueTempDirUnderPath(sub_dir2.GetPath()));
  base::ScopedTempDir sub_dir3;
  ASSERT_TRUE(sub_dir3.CreateUniqueTempDirUnderPath(my_files_dir_));
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, sub_dir1.GetPath().AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, sub_dir1.GetPath().AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, sub_dir2_1.GetPath().AppendASCII(kFilePath3),
                     kExampleUrl3),
      FileDaemonInfo(kInode4, sub_dir2_1.GetPath().AppendASCII(kFilePath4),
                     kExampleUrl4),
      FileDaemonInfo(kInode5, sub_dir3.GetPath().AppendASCII(kFilePath5),
                     kExampleUrl5)};
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<ui::SelectedFileInfo> selected_files(
      {{sub_dir1.GetPath(), sub_dir1.GetPath()},
       {sub_dir2_1.GetPath(), sub_dir2_1.GetPath()},
       {sub_dir3.GetPath(), sub_dir3.GetPath()}});

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(files_urls[0].path().value());
  check_files_transfer_response.add_files_paths(files_urls[1].path().value());
  check_files_transfer_response.add_files_paths(files_urls[3].path().value());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{absl::nullopt},
                          std::vector<base::FilePath>{files_urls[0].path(),
                                                      files_urls[1].path(),
                                                      files_urls[3].path()},
                          dlp::FileAction::kUpload));

  base::test::TestFuture<std::vector<ui::SelectedFileInfo>> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->FilterDisallowedUploads(
      selected_files, DlpFileDestination(kExampleUrl1), future.GetCallback());

  std::vector<ui::SelectedFileInfo> expected_filtered_uploads(
      {{sub_dir3.GetPath(), sub_dir3.GetPath()}});
  ASSERT_EQ(1u, future.Get().size());
  EXPECT_EQ(expected_filtered_uploads, future.Take());

  // Validate the request sent to the daemon.
  std::vector<std::string> expected_requested_files;
  for (const auto& file_url : files_urls) {
    expected_requested_files.push_back(file_url.path().value());
  }

  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  std::vector<std::string> requested_files(request.files_paths().begin(),
                                           request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
  EXPECT_EQ(kExampleUrl1, request.destination_url());
  EXPECT_EQ(::dlp::FileAction::UPLOAD, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

TEST_F(DlpFilesControllerAshTest, GetDlpMetadata) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> files_to_check(
      {files_urls[0], files_urls[1], files_urls[2]});
  std::vector<DlpFilesControllerAsh::DlpFileMetadata> dlp_metadata(
      {DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl1, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/false),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl2, /*is_dlp_restricted=*/false,
           /*is_restricted_for_destination=*/false),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl3, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/false)});

  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule)
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock))
      .WillOnce(testing::Return(DlpRulesManager::Level::kAllow))
      .WillOnce(testing::Return(DlpRulesManager::Level::kWarn));
  // If destination is not passed, neither of these should be called.
  EXPECT_CALL(*rules_manager_, IsRestrictedDestination).Times(0);
  EXPECT_CALL(*rules_manager_, IsRestrictedComponent).Times(0);

  base::test::TestFuture<std::vector<DlpFilesControllerAsh::DlpFileMetadata>>
      future;
  ASSERT_TRUE(files_controller_);
  files_controller_->GetDlpMetadata(files_to_check, absl::nullopt,
                                    future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(dlp_metadata, future.Take());
}

TEST_F(DlpFilesControllerAshTest, GetDlpMetadata_WithComponent) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> files_to_check(
      {files_urls[0], files_urls[1], files_urls[2]});
  std::vector<DlpFilesControllerAsh::DlpFileMetadata> dlp_metadata(
      {DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl1, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/true),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl2, /*is_dlp_restricted=*/false,
           /*is_restricted_for_destination=*/false),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl3, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/false)});

  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule)
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock))
      .WillOnce(testing::Return(DlpRulesManager::Level::kAllow))
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  // If destination is passed as component, the restriction should be checked if
  // there are files with any "block" restriction.
  EXPECT_CALL(*rules_manager_, IsRestrictedComponent)
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock))
      .WillOnce(testing::Return(DlpRulesManager::Level::kWarn))
      .RetiresOnSaturation();
  EXPECT_CALL(*rules_manager_, IsRestrictedDestination).Times(0);

  base::test::TestFuture<std::vector<DlpFilesControllerAsh::DlpFileMetadata>>
      future;
  ASSERT_TRUE(files_controller_);
  files_controller_->GetDlpMetadata(
      files_to_check, DlpFileDestination(data_controls::Component::kUsb),
      future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(dlp_metadata, future.Take());
}

TEST_F(DlpFilesControllerAshTest, GetDlpMetadata_WithDestination) {
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<storage::FileSystemURL> files_to_check(
      {files_urls[0], files_urls[1], files_urls[2]});
  std::vector<DlpFilesControllerAsh::DlpFileMetadata> dlp_metadata(
      {DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl1, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/true),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl2, /*is_dlp_restricted=*/false,
           /*is_restricted_for_destination=*/false),
       DlpFilesControllerAsh::DlpFileMetadata(
           kExampleUrl3, /*is_dlp_restricted=*/true,
           /*is_restricted_for_destination=*/false)});

  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule)
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock))
      .WillOnce(testing::Return(DlpRulesManager::Level::kAllow))
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  // If destination is passed as url, the restriction should be checked if there
  // are files with any "block" restriction.
  EXPECT_CALL(*rules_manager_, IsRestrictedDestination)
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock))
      .WillOnce(testing::Return(DlpRulesManager::Level::kWarn))
      .RetiresOnSaturation();
  EXPECT_CALL(*rules_manager_, IsRestrictedComponent).Times(0);

  base::test::TestFuture<std::vector<DlpFilesControllerAsh::DlpFileMetadata>>
      future;
  ASSERT_TRUE(files_controller_);
  files_controller_->GetDlpMetadata(
      files_to_check, DlpFileDestination(kExampleUrl1), future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(dlp_metadata, future.Take());
}

TEST_F(DlpFilesControllerAshTest, GetDlpMetadata_FileNotAvailable) {
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());

  auto file_path = my_files_dir_.AppendASCII(kFilePath1);
  ASSERT_TRUE(CreateDummyFile(file_path));
  auto file_url = CreateFileSystemURL(file_path.value());
  ASSERT_TRUE(file_url.is_valid());

  std::vector<storage::FileSystemURL> files_to_check({file_url});
  std::vector<DlpFilesControllerAsh::DlpFileMetadata> dlp_metadata(
      {DlpFilesControllerAsh::DlpFileMetadata(
          /*source_url=*/"", /*is_dlp_restricted=*/false,
          /*is_restricted_for_destination=*/false)});

  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule).Times(0);

  base::test::TestFuture<std::vector<DlpFilesControllerAsh::DlpFileMetadata>>
      future;
  ASSERT_TRUE(files_controller_);
  files_controller_->GetDlpMetadata(files_to_check, absl::nullopt,
                                    future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(dlp_metadata, future.Take());
}

TEST_F(DlpFilesControllerAshTest, GetDlpRestrictionDetails_Mixed) {
  DlpRulesManager::AggregatedDestinations destinations;
  destinations[DlpRulesManager::Level::kBlock].insert(kExampleUrl2);
  destinations[DlpRulesManager::Level::kAllow].insert(kExampleUrl3);

  DlpRulesManager::AggregatedComponents components;
  components[DlpRulesManager::Level::kBlock].insert(
      data_controls::Component::kUsb);
  components[DlpRulesManager::Level::kWarn].insert(
      data_controls::Component::kDrive);
  components[DlpRulesManager::Level::kReport].insert(
      data_controls::Component::kOneDrive);

  EXPECT_CALL(*rules_manager_, GetAggregatedDestinations)
      .WillOnce(testing::Return(destinations));
  EXPECT_CALL(*rules_manager_, GetAggregatedComponents)
      .WillOnce(testing::Return(components));

  ASSERT_TRUE(files_controller_);
  auto result = files_controller_->GetDlpRestrictionDetails(kExampleUrl1);

  ASSERT_EQ(result.size(), 4u);
  std::vector<std::string> expected_urls;
  std::vector<data_controls::Component> expected_components;
  // Block:
  expected_urls.push_back(kExampleUrl2);
  expected_components.push_back(data_controls::Component::kUsb);
  EXPECT_EQ(result[0].level, DlpRulesManager::Level::kBlock);
  EXPECT_EQ(result[0].urls, expected_urls);
  EXPECT_EQ(result[0].components, expected_components);
  // Allow:
  expected_urls.clear();
  expected_urls.push_back(kExampleUrl3);
  expected_components.clear();
  EXPECT_EQ(result[1].level, DlpRulesManager::Level::kAllow);
  EXPECT_EQ(result[1].urls, expected_urls);
  EXPECT_EQ(result[1].components, expected_components);
  // Report:
  expected_urls.clear();
  expected_components.clear();
  expected_components.push_back(data_controls::Component::kOneDrive);
  EXPECT_EQ(result[2].level, DlpRulesManager::Level::kReport);
  EXPECT_EQ(result[2].urls, expected_urls);
  EXPECT_EQ(result[2].components, expected_components);
  // Warn:
  expected_urls.clear();
  expected_components.clear();
  expected_components.push_back(data_controls::Component::kDrive);
  EXPECT_EQ(result[3].level, DlpRulesManager::Level::kWarn);
  EXPECT_EQ(result[3].urls, expected_urls);
  EXPECT_EQ(result[3].components, expected_components);
}

TEST_F(DlpFilesControllerAshTest, GetDlpRestrictionDetails_Components) {
  DlpRulesManager::AggregatedDestinations destinations;
  DlpRulesManager::AggregatedComponents components;
  components[DlpRulesManager::Level::kBlock].insert(
      data_controls::Component::kUsb);

  EXPECT_CALL(*rules_manager_, GetAggregatedDestinations)
      .WillOnce(testing::Return(destinations));
  EXPECT_CALL(*rules_manager_, GetAggregatedComponents)
      .WillOnce(testing::Return(components));

  ASSERT_TRUE(files_controller_);
  auto result = files_controller_->GetDlpRestrictionDetails(kExampleUrl1);
  ASSERT_EQ(result.size(), 1u);
  std::vector<std::string> expected_urls;
  std::vector<data_controls::Component> expected_components;
  expected_components.push_back(data_controls::Component::kUsb);
  EXPECT_EQ(result[0].level, DlpRulesManager::Level::kBlock);
  EXPECT_EQ(result[0].urls, expected_urls);
  EXPECT_EQ(result[0].components, expected_components);
}

TEST_F(DlpFilesControllerAshTest, GetBlockedComponents) {
  DlpRulesManager::AggregatedComponents components;
  components[DlpRulesManager::Level::kBlock].insert(
      data_controls::Component::kArc);
  components[DlpRulesManager::Level::kBlock].insert(
      data_controls::Component::kCrostini);
  components[DlpRulesManager::Level::kWarn].insert(
      data_controls::Component::kUsb);
  components[DlpRulesManager::Level::kReport].insert(
      data_controls::Component::kDrive);

  EXPECT_CALL(*rules_manager_, GetAggregatedComponents)
      .WillOnce(testing::Return(components));

  ASSERT_TRUE(files_controller_);
  auto result = files_controller_->GetBlockedComponents(kExampleUrl1);
  ASSERT_EQ(result.size(), 2u);
  std::vector<data_controls::Component> expected_components;
  expected_components.push_back(data_controls::Component::kArc);
  expected_components.push_back(data_controls::Component::kCrostini);
  EXPECT_EQ(result, expected_components);
}

TEST_F(DlpFilesControllerAshTest, DownloadToLocalAllowed) {
  MockCheckIfDlpAllowedCallback cb;
  EXPECT_CALL(cb, Run(/*is_allowed=*/true)).Times(1);

  files_controller_->CheckIfDownloadAllowed(
      DlpFileDestination(kExampleUrl1),
      base::FilePath(
          "/home/chronos/u-0123456789abcdef/MyFiles/Downloads/img.jpg"),
      cb.Get());
}

TEST_F(DlpFilesControllerAshTest, CheckReportingOnIsDlpPolicyMatched) {
  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule)
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern1),
                         testing::SetArgPointee<3>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern2),
                         testing::SetArgPointee<3>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kReport)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern3),
                         testing::SetArgPointee<3>(kRuleMetadata3),
                         testing::Return(DlpRulesManager::Level::kWarn)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern4),
                         testing::SetArgPointee<3>(kRuleMetadata4),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern1),
                         testing::SetArgPointee<3>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern2),
                         testing::SetArgPointee<3>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kReport)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern3),
                         testing::SetArgPointee<3>(kRuleMetadata3),
                         testing::Return(DlpRulesManager::Level::kWarn)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern1),
                         testing::SetArgPointee<3>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern2),
                         testing::SetArgPointee<3>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kReport)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern1),
                         testing::SetArgPointee<3>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kWarn)));

  EXPECT_CALL(*rules_manager_, GetReportingManager).Times(testing::AnyNumber());

  const auto histogram_tester = base::HistogramTester();

  const auto file1 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode1, base::FilePath(kFilePath1), kExampleUrl1);
  const auto file2 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode2, base::FilePath(kFilePath2), kExampleUrl2);
  const auto file3 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode3, base::FilePath(kFilePath3), kExampleUrl3);
  const auto file4 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode4, base::FilePath(kFilePath4), kExampleUrl4);

  auto CreateEvent =
      [](const std::string& src_pattern, DlpRulesManager::Level level,
         const std::string& filename, const std::string& rule_name,
         const std::string& rule_id) {
        auto event_builder = DlpPolicyEventBuilder::Event(
            src_pattern, rule_name, rule_id,
            DlpRulesManager::Restriction::kFiles, level);
        event_builder->SetDestinationComponent(
            data_controls::Component::kUnknownComponent);
        event_builder->SetContentName(filename);
        return event_builder->Create();
      };

  const auto event1 =
      CreateEvent(kExampleSourcePattern1, DlpRulesManager::Level::kBlock,
                  kFilePath1, kRuleName1, kRuleId1);
  const auto event2 =
      CreateEvent(kExampleSourcePattern2, DlpRulesManager::Level::kReport,
                  kFilePath2, kRuleName2, kRuleId2);
  const auto event3 =
      CreateEvent(kExampleSourcePattern3, DlpRulesManager::Level::kWarn,
                  kFilePath3, kRuleName3, kRuleId3);

  base::TimeDelta cooldown_time =
      event_storage_->GetDeduplicationCooldownForTesting();

  // Report `event1`, `event2`, and `event3` after these calls.
  ASSERT_TRUE(files_controller_->IsDlpPolicyMatched(file1));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file2));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file3));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file4));

  task_runner_->FastForwardBy(cooldown_time);

  // Report `event1`, `event2`, and `event3` after these calls.
  ASSERT_TRUE(files_controller_->IsDlpPolicyMatched(file1));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file2));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file3));

  task_runner_->FastForwardBy(cooldown_time / 2);

  // Do not report after these calls.
  ASSERT_TRUE(files_controller_->IsDlpPolicyMatched(file1));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file2));
  ASSERT_FALSE(files_controller_->IsDlpPolicyMatched(file3));

  const auto expected_events = std::vector<const DlpPolicyEvent*>(
      {&event1, &event2, &event3, &event1, &event2, &event3});

  ASSERT_EQ(events.size(), 6u);
  for (size_t i = 0; i < events.size(); ++i) {
    EXPECT_THAT(events[i], IsDlpPolicyEvent(*expected_events[i]));
  }

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionBlockedUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kUnknown, 3),
                       base::Bucket(dlp::FileAction::kDownload, 0),
                       base::Bucket(dlp::FileAction::kTransfer, 0)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionWarnedUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kUnknown, 3),
                       base::Bucket(dlp::FileAction::kDownload, 0),
                       base::Bucket(dlp::FileAction::kTransfer, 0)));
}

TEST_F(DlpFilesControllerAshTest, CheckReportingOnIsFilesTransferRestricted) {
  const auto histogram_tester = base::HistogramTester();

  const auto file1 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode1, base::FilePath(kFilePath1), kExampleUrl1);
  const auto file2 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode2, base::FilePath(kFilePath2), kExampleUrl2);

  const std::string dst_url = "https://wetransfer.com/";
  const std::string dst_pattern = "wetransfer.com";

  EXPECT_CALL(*rules_manager_, IsRestrictedDestination(_, _, _, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)));

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, data_controls::Component::kUsb, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  mount_points->RevokeAllFileSystems();

  ASSERT_TRUE(mount_points->RegisterFileSystem(
      ash::kSystemMountNameRemovable, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kRemovableMediaPath)));

  auto dst_path = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "removable",
      base::FilePath("MyUSB/path/in/removable"));
  ASSERT_TRUE(dst_path.is_valid());

  std::vector<DlpFilesControllerAsh::FileDaemonInfo> transferred_files = {
      file1, file2};
  std::vector<std::pair<FileDaemonInfo, ::dlp::RestrictionLevel>> files_levels =
      {{file1, ::dlp::RestrictionLevel::LEVEL_BLOCK},
       {file2, ::dlp::RestrictionLevel::LEVEL_ALLOW}};

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(::testing::AnyNumber());

  auto event_builder = DlpPolicyEventBuilder::Event(
      kExampleSourcePattern1, kRuleName1, kRuleId1,
      DlpRulesManager::Restriction::kFiles, DlpRulesManager::Level::kBlock);
  event_builder->SetContentName(kFilePath1);

  event_builder->SetDestinationPattern(dst_pattern);
  const auto event1 = event_builder->Create();

  event_builder->SetDestinationComponent(data_controls::Component::kUsb);
  const auto event2 = event_builder->Create();

  base::TimeDelta cooldown_time =
      event_storage_->GetDeduplicationCooldownForTesting();

  std::vector<base::TimeDelta> delays = {cooldown_time / 2, cooldown_time,
                                         base::Seconds(0)};

  for (base::TimeDelta delay : delays) {
    // Report `event1` after this call if `delay` is at least `cooldown_time`.
    files_controller_->IsFilesTransferRestricted(
        /*task_id=*/1234, transferred_files, DlpFileDestination(dst_url),
        dlp::FileAction::kTransfer, cb.Get());

    // Report `event2` after this call if `delay` is at least `cooldown_time`.
    files_controller_->IsFilesTransferRestricted(
        /*task_id=*/1234, transferred_files,
        DlpFileDestination(dst_path.path().value()), dlp::FileAction::kTransfer,
        cb.Get());

    task_runner_->FastForwardBy(delay);
  }

  const auto expected_events =
      std::vector<const DlpPolicyEvent*>({&event1, &event2, &event1, &event2});

  ASSERT_EQ(events.size(), 4u);
  for (size_t i = 0; i < events.size(); ++i) {
    EXPECT_THAT(events[i], IsDlpPolicyEvent(*expected_events[i]));
  }
}

TEST_F(DlpFilesControllerAshTest, CheckReportingOnMixedCalls) {
  const auto file1 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode1, base::FilePath(kFilePath1), kExampleUrl1);
  const auto file2 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode2, base::FilePath(kFilePath2), kExampleUrl2);

  const std::string dst_url = "https://wetransfer.com/";
  const std::string dst_pattern = "wetransfer.com";

  EXPECT_CALL(*rules_manager_, IsRestrictedByAnyRule)
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<2>(kExampleSourcePattern1),
                         testing::Return(DlpRulesManager::Level::kBlock)));

  EXPECT_CALL(*rules_manager_, IsRestrictedDestination(_, _, _, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(dst_pattern),
                         testing::SetArgPointee<5>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  std::vector<DlpFilesControllerAsh::FileDaemonInfo> transferred_files = {
      file1, file2};
  std::vector<std::pair<FileDaemonInfo, ::dlp::RestrictionLevel>> files_levels =
      {{file1, ::dlp::RestrictionLevel::LEVEL_BLOCK},
       {file2, ::dlp::RestrictionLevel::LEVEL_ALLOW}};

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(1);

  auto event_builder = DlpPolicyEventBuilder::Event(
      kExampleSourcePattern1, kRuleName1, kRuleId1,
      DlpRulesManager::Restriction::kFiles, DlpRulesManager::Level::kBlock);
  event_builder->SetContentName(kFilePath1);
  event_builder->SetDestinationPattern(dst_pattern);
  const auto event = event_builder->Create();

  // Report a single `event` after this call
  files_controller_->IsFilesTransferRestricted(
      /*task_id=*/2345, transferred_files, DlpFileDestination(dst_url),
      dlp::FileAction::kTransfer, cb.Get());

  // Do not report after these calls
  ASSERT_TRUE(files_controller_->IsDlpPolicyMatched(file1));

  ASSERT_EQ(events.size(), 1u);
  EXPECT_THAT(events[0], IsDlpPolicyEvent(event));
}

TEST_F(DlpFilesControllerAshTest, CheckIfDropAllowed_ErrorResponse) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  base::FilePath file_path1 = my_files_dir_.AppendASCII(kFilePath1);
  ASSERT_TRUE(CreateDummyFile(file_path1));
  auto file_url1 = CreateFileSystemURL(file_path1.value());

  // Set CheckFilesTransferResponse to return an error.
  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(file_path1.value());
  check_files_transfer_response.set_error_message("Did not receive a reply.");
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  std::vector<ui::FileInfo> dropped_files{ui::FileInfo(file_path1, file_path1)};
  const ui::DataTransferEndpoint data_dst((GURL(kExampleUrl1)));

  base::test::TestFuture<bool> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfDropAllowed(std::move(dropped_files), &data_dst,
                                        future.GetCallback());

  ASSERT_EQ(true, future.Get());

  // Validate the request sent to the daemon.
  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  ASSERT_EQ(request.files_paths().size(), 1);
  EXPECT_EQ(request.files_paths()[0], file_path1.value());
  EXPECT_EQ(kExampleUrl1, request.destination_url());
  EXPECT_EQ(::dlp::FileAction::MOVE, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

// Tests dropping a mix of an external file and a local directory.
TEST_F(DlpFilesControllerAshTest, CheckIfDropAllowed) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  base::ScopedTempDir external_dir;
  ASSERT_TRUE(external_dir.CreateUniqueTempDir());
  base::FilePath file_path1 = external_dir.GetPath().AppendASCII(kFilePath1);
  ASSERT_TRUE(CreateDummyFile(file_path1));
  auto file_url1 = CreateFileSystemURL(file_path1.value());

  base::ScopedTempDir sub_dir1;
  ASSERT_TRUE(sub_dir1.CreateUniqueTempDirUnderPath(my_files_dir_));
  base::FilePath file_path2 = sub_dir1.GetPath().AppendASCII(kFilePath2);
  ASSERT_TRUE(CreateDummyFile(file_path2));
  auto file_url2 = CreateFileSystemURL(file_path2.value());

  // Set CheckFilesTransfer response to restrict the local file.
  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(file_path2.value());
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  EXPECT_CALL(*fpnm_,
              ShowDlpBlockedFiles(/*task_id=*/{absl::nullopt},
                                  std::vector<base::FilePath>{file_path2},
                                  dlp::FileAction::kMove));

  std::vector<ui::FileInfo> dropped_files{
      ui::FileInfo(file_path1, file_path1),
      ui::FileInfo(sub_dir1.GetPath(), sub_dir1.GetPath())};
  const ui::DataTransferEndpoint data_dst((GURL(kExampleUrl1)));

  base::test::TestFuture<bool> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfDropAllowed(dropped_files, &data_dst,
                                        future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(false, future.Take());

  // Validate that only the local file was sent to the daemon.
  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  ASSERT_EQ(request.files_paths().size(), 1);
  EXPECT_EQ(request.files_paths()[0], file_path2.value());
  EXPECT_EQ(kExampleUrl1, request.destination_url());
  EXPECT_EQ(::dlp::FileAction::MOVE, request.file_action());
  EXPECT_FALSE(request.has_io_task_id());
}

TEST_F(DlpFilesControllerAshTest, IsFilesTransferRestricted_MyFiles) {
  const auto histogram_tester = base::HistogramTester();

  auto fileDaemonInfo1 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode1, base::FilePath(), kExampleUrl1);
  auto fileDaemonInfo2 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode2, base::FilePath(), kExampleUrl2);
  auto fileDaemonInfo3 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode3, base::FilePath(), kExampleUrl3);

  std::vector<DlpFilesControllerAsh::FileDaemonInfo> transferred_files(
      {fileDaemonInfo1, fileDaemonInfo2, fileDaemonInfo3});
  std::vector<std::pair<FileDaemonInfo, ::dlp::RestrictionLevel>> files_levels =
      {{fileDaemonInfo1, ::dlp::RestrictionLevel::LEVEL_ALLOW},
       {fileDaemonInfo2, ::dlp::RestrictionLevel::LEVEL_ALLOW},
       {fileDaemonInfo3, ::dlp::RestrictionLevel::LEVEL_ALLOW}};

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(1);

  EXPECT_CALL(*rules_manager_, IsRestrictedComponent).Times(0);

  EXPECT_CALL(*rules_manager_, IsRestrictedDestination).Times(0);

  EXPECT_CALL(*rules_manager_, GetReportingManager()).Times(0);

  files_controller_->IsFilesTransferRestricted(
      /*task_id=*/1234, transferred_files,
      DlpFileDestination(my_files_dir_url_.path().value()),
      dlp::FileAction::kTransfer, cb.Get());

  ASSERT_EQ(events.size(), 0u);

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionBlockedUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kUnknown, 0),
                       base::Bucket(dlp::FileAction::kDownload, 0),
                       base::Bucket(dlp::FileAction::kTransfer, 0)));
}

class DlpFilesTestWithMounts : public DlpFilesControllerAshTest {
 public:
  DlpFilesTestWithMounts(const DlpFilesTestWithMounts&) = delete;
  DlpFilesTestWithMounts& operator=(const DlpFilesTestWithMounts&) = delete;

 protected:
  DlpFilesTestWithMounts() = default;

  ~DlpFilesTestWithMounts() override = default;

  void SetUp() override {
    DlpFilesControllerAshTest::SetUp();

    mount_points_ = storage::ExternalMountPoints::GetSystemInstance();
    ASSERT_TRUE(mount_points_);

    mount_points_->RevokeAllFileSystems();

    ASSERT_TRUE(mount_points_->RegisterFileSystem(
        file_manager::util::GetAndroidFilesMountPointName(),
        storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
        base::FilePath(file_manager::util::GetAndroidFilesPath())));

    ASSERT_TRUE(mount_points_->RegisterFileSystem(
        ash::kSystemMountNameRemovable, storage::kFileSystemTypeLocal,
        storage::FileSystemMountOption(),
        base::FilePath(file_manager::util::kRemovableMediaPath)));

    // Setup for Crostini.
    crostini::FakeCrostiniFeatures crostini_features;
    crostini_features.set_is_allowed_now(true);
    crostini_features.set_enabled(true);

    ash::ChunneldClient::InitializeFake();
    ash::CiceroneClient::InitializeFake();
    ash::ConciergeClient::InitializeFake();
    ash::SeneschalClient::InitializeFake();

    crostini::CrostiniManager* crostini_manager =
        crostini::CrostiniManager::GetForProfile(profile_.get());
    ASSERT_TRUE(crostini_manager);
    crostini_manager->AddRunningVmForTesting(crostini::kCrostiniDefaultVmName);
    crostini_manager->AddRunningContainerForTesting(
        crostini::kCrostiniDefaultVmName,
        crostini::ContainerInfo(crostini::kCrostiniDefaultContainerName,
                                "testuser", "/home/testuser",
                                "PLACEHOLDER_IP"));
    ASSERT_TRUE(mount_points_->RegisterFileSystem(
        file_manager::util::GetCrostiniMountPointName(profile_.get()),
        storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
        file_manager::util::GetCrostiniMountDirectory(profile_.get())));

    // Setup for DriveFS.
    profile_->GetPrefs()->SetString(drive::prefs::kDriveFsProfileSalt, "a");
    drive::DriveIntegrationServiceFactory::GetForProfile(profile_.get())
        ->SetEnabled(true);
    drive::DriveIntegrationService* integration_service =
        drive::DriveIntegrationServiceFactory::GetForProfile(profile_.get());
    ASSERT_TRUE(integration_service);
    base::FilePath mount_point_drive = integration_service->GetMountPointPath();
    ASSERT_TRUE(mount_points_->RegisterFileSystem(
        mount_point_drive.BaseName().value(), storage::kFileSystemTypeLocal,
        storage::FileSystemMountOption(), mount_point_drive));
  }

  void TearDown() override {
    DlpFilesControllerAshTest::TearDown();

    ash::ChunneldClient::Shutdown();
    ash::CiceroneClient::Shutdown();
    ash::ConciergeClient::Shutdown();
    ash::SeneschalClient::Shutdown();

    storage::ExternalMountPoints::GetSystemInstance()->RevokeAllFileSystems();
  }

  raw_ptr<storage::ExternalMountPoints, ExperimentalAsh> mount_points_ =
      nullptr;
};

class DlpFilesExternalDestinationTest
    : public DlpFilesTestWithMounts,
      public ::testing::WithParamInterface<
          std::tuple<std::string, std::string, data_controls::Component>> {
 public:
  DlpFilesExternalDestinationTest(const DlpFilesExternalDestinationTest&) =
      delete;
  DlpFilesExternalDestinationTest& operator=(
      const DlpFilesExternalDestinationTest&) = delete;

 protected:
  DlpFilesExternalDestinationTest() = default;

  ~DlpFilesExternalDestinationTest() override = default;
};

INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesExternalDestinationTest,
    ::testing::Values(
        std::make_tuple("android_files",
                        "path/in/android/filename",
                        data_controls::Component::kArc),
        std::make_tuple("removable",
                        "MyUSB/path/in/removable/filename",
                        data_controls::Component::kUsb),
        std::make_tuple("crostini_test_termina_penguin",
                        "path/in/crostini/filename",
                        data_controls::Component::kCrostini),
        std::make_tuple("drivefs-84675c855b63e12f384d45f033826980",
                        "root/path/in/mydrive/filename",
                        data_controls::Component::kDrive)));

TEST_P(DlpFilesExternalDestinationTest, IsFilesTransferRestricted_Component) {
  auto [mount_name, path, expected_component] = GetParam();

  const auto histogram_tester = base::HistogramTester();

  auto fileDaemonInfo1 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode1, base::FilePath(), kExampleUrl1);
  auto fileDaemonInfo2 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode2, base::FilePath(), kExampleUrl2);
  auto fileDaemonInfo3 = DlpFilesControllerAsh::FileDaemonInfo(
      kInode3, base::FilePath(), kExampleUrl3);

  std::vector<DlpFilesControllerAsh::FileDaemonInfo> transferred_files(
      {fileDaemonInfo1, fileDaemonInfo2, fileDaemonInfo3});
  std::vector<std::pair<FileDaemonInfo, ::dlp::RestrictionLevel>> files_levels =
      {{fileDaemonInfo1, ::dlp::RestrictionLevel::LEVEL_BLOCK},
       {fileDaemonInfo2, ::dlp::RestrictionLevel::LEVEL_ALLOW},
       {fileDaemonInfo3, ::dlp::RestrictionLevel::LEVEL_BLOCK}};

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(1);

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, expected_component, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern2),
                         testing::SetArgPointee<4>(kRuleMetadata2),
                         testing::Return(DlpRulesManager::Level::kAllow)))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern3),
                         testing::SetArgPointee<4>(kRuleMetadata3),
                         testing::Return(DlpRulesManager::Level::kBlock)));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  auto dst_url = mount_points_->CreateExternalFileSystemURL(
      blink::StorageKey(), mount_name, base::FilePath(path));
  ASSERT_TRUE(dst_url.is_valid());

  files_controller_->IsFilesTransferRestricted(
      /*task_id=*/1234, transferred_files,
      DlpFileDestination(dst_url.path().value()), dlp::FileAction::kTransfer,
      cb.Get());

  ASSERT_EQ(events.size(), 2u);
  EXPECT_THAT(events[0], IsDlpPolicyEvent(CreateDlpPolicyEvent(
                             kExampleSourcePattern1, expected_component,
                             DlpRulesManager::Restriction::kFiles, kRuleName1,
                             kRuleId1, DlpRulesManager::Level::kBlock)));
  EXPECT_THAT(events[1], IsDlpPolicyEvent(CreateDlpPolicyEvent(
                             kExampleSourcePattern3, expected_component,
                             DlpRulesManager::Restriction::kFiles, kRuleName3,
                             kRuleId3, DlpRulesManager::Level::kBlock)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionBlockedUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kUnknown, 0),
                       base::Bucket(dlp::FileAction::kDownload, 0),
                       base::Bucket(dlp::FileAction::kTransfer, 2)));
}

TEST_P(DlpFilesExternalDestinationTest, FileDownloadBlocked) {
  auto [mount_name, path, expected_component] = GetParam();

  MockCheckIfDlpAllowedCallback cb;
  EXPECT_CALL(cb, Run(/*is_allowed=*/false)).Times(1);

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, expected_component, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kBlock)));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  auto dst_url = mount_points_->CreateExternalFileSystemURL(
      blink::StorageKey(), mount_name, base::FilePath(path));
  ASSERT_TRUE(dst_url.is_valid());

  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{absl::nullopt},
                          std::vector<base::FilePath>{dst_url.path()},
                          dlp::FileAction::kDownload));

  files_controller_->CheckIfDownloadAllowed(DlpFileDestination(kExampleUrl1),
                                            dst_url.path(), cb.Get());

  ASSERT_EQ(events.size(), 1u);

  auto event_builder = DlpPolicyEventBuilder::Event(
      kExampleSourcePattern1, kRuleName1, kRuleId1,
      DlpRulesManager::Restriction::kFiles, DlpRulesManager::Level::kBlock);

  event_builder->SetDestinationComponent(expected_component);
  event_builder->SetContentName(base::FilePath(path).BaseName().value());

  EXPECT_THAT(events[0], IsDlpPolicyEvent(event_builder->Create()));
}

TEST_P(DlpFilesExternalDestinationTest, FilePromptForDownload) {
  auto [mount_name, path, expected_component] = GetParam();

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, expected_component, _, _, _))
      .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));

  auto dst_url = mount_points_->CreateExternalFileSystemURL(
      blink::StorageKey(), mount_name, base::FilePath(path));
  ASSERT_TRUE(dst_url.is_valid());

  EXPECT_TRUE(files_controller_->ShouldPromptBeforeDownload(
      DlpFileDestination(kExampleUrl1), dst_url.path()));
}

class DlpFilesUrlDestinationTest
    : public DlpFilesControllerAshTest,
      public ::testing::WithParamInterface<
          std::tuple<std::vector<DlpRulesManager::Level>,
                     std::string,
                     std::string>> {
 protected:
  std::vector<DlpFilesControllerAsh::FileDaemonInfo> transferred_files{
      DlpFilesControllerAsh::FileDaemonInfo(kInode1,
                                            base::FilePath(),
                                            kExampleUrl1),
      DlpFilesControllerAsh::FileDaemonInfo(kInode2,
                                            base::FilePath(),
                                            kExampleUrl2),
      DlpFilesControllerAsh::FileDaemonInfo(kInode3,
                                            base::FilePath(),
                                            kExampleUrl3)};
  std::vector<std::string> source_patterns = {
      kExampleSourcePattern1, kExampleSourcePattern2, kExampleSourcePattern3};
  std::vector<std::string> rules_names = {kRuleName1, kRuleName2, kRuleName3};
  std::vector<std::string> rules_ids = {kRuleId1, kRuleId2, kRuleId3};
};
INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesUrlDestinationTest,
    ::testing::Values(
        std::make_tuple(
            std::vector<DlpRulesManager::Level>{DlpRulesManager::Level::kBlock,
                                                DlpRulesManager::Level::kAllow,
                                                DlpRulesManager::Level::kBlock},
            "https://wetransfer.com/",
            "wetransfer.com"),
        std::make_tuple(
            std::vector<DlpRulesManager::Level>{DlpRulesManager::Level::kAllow,
                                                DlpRulesManager::Level::kAllow,
                                                DlpRulesManager::Level::kAllow},
            "https://drive.google.com/",
            "google.com")));

TEST_P(DlpFilesUrlDestinationTest, IsFilesTransferRestricted_Url) {
  auto [levels, destination_url, destination_pattern] = GetParam();
  ASSERT_EQ(levels.size(), transferred_files.size());

  const auto histogram_tester = base::HistogramTester();

  std::vector<std::pair<FileDaemonInfo, ::dlp::RestrictionLevel>> files_levels;
  std::vector<std::string> disallowed_source_patterns;
  std::vector<std::string> triggered_rule_names;
  std::vector<std::string> triggered_rule_ids;
  for (size_t i = 0; i < transferred_files.size(); ++i) {
    if (levels[i] == DlpRulesManager::Level::kBlock) {
      files_levels.emplace_back(transferred_files[i],
                                ::dlp::RestrictionLevel::LEVEL_BLOCK);
      disallowed_source_patterns.emplace_back(source_patterns[i]);
      triggered_rule_names.emplace_back(rules_names[i]);
      triggered_rule_ids.emplace_back(rules_ids[i]);
    } else {
      files_levels.emplace_back(transferred_files[i],
                                ::dlp::RestrictionLevel::LEVEL_ALLOW);
    }
  }
  EXPECT_CALL(*rules_manager_, IsRestrictedDestination(_, _, _, _, _, _))
      .WillOnce(testing::DoAll(testing::SetArgPointee<3>(source_patterns[0]),
                               testing::SetArgPointee<4>(destination_pattern),
                               testing::SetArgPointee<5>(kRuleMetadata1),
                               testing::Return(levels[0])))
      .WillOnce(testing::DoAll(testing::SetArgPointee<3>(source_patterns[1]),
                               testing::SetArgPointee<4>(destination_pattern),
                               testing::SetArgPointee<5>(kRuleMetadata2),
                               testing::Return(levels[1])))
      .WillOnce(testing::DoAll(testing::SetArgPointee<3>(source_patterns[2]),
                               testing::SetArgPointee<4>(destination_pattern),
                               testing::SetArgPointee<5>(kRuleMetadata3),
                               testing::Return(levels[2])));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(1);

  files_controller_->IsFilesTransferRestricted(
      /*task_id=*/absl::nullopt, transferred_files,
      DlpFileDestination(destination_url), dlp::FileAction::kDownload,
      cb.Get());

  ASSERT_EQ(events.size(), disallowed_source_patterns.size());
  for (size_t i = 0u; i < disallowed_source_patterns.size(); ++i) {
    EXPECT_THAT(
        events[i],
        IsDlpPolicyEvent(CreateDlpPolicyEvent(
            disallowed_source_patterns[i], destination_pattern,
            DlpRulesManager::Restriction::kFiles, triggered_rule_names[i],
            triggered_rule_ids[i], DlpRulesManager::Level::kBlock)));
  }

  int blocked_downloads = disallowed_source_patterns.empty()
                              ? 0
                              : disallowed_source_patterns.size();

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionBlockedUMA)),
      base::BucketsAre(
          base::Bucket(dlp::FileAction::kDownload, blocked_downloads),
          base::Bucket(dlp::FileAction::kTransfer, 0)));
}

class DlpFilesWarningDialogChoiceTest
    : public DlpFilesControllerAshTest,
      public ::testing::WithParamInterface<bool> {};

INSTANTIATE_TEST_SUITE_P(DlpFiles,
                         DlpFilesWarningDialogChoiceTest,
                         ::testing::Bool());

TEST_P(DlpFilesWarningDialogChoiceTest, FileDownloadWarned) {
  bool choice_result = GetParam();

  const auto histogram_tester = base::HistogramTester();

  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  mount_points->RevokeAllFileSystems();
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      ash::kSystemMountNameRemovable, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kRemovableMediaPath)));

  EXPECT_CALL(*fpnm_, ShowDlpWarning)
      .WillOnce([&choice_result](
                    OnDlpRestrictionCheckedCallback callback,
                    absl::optional<file_manager::io_task::IOTaskId> task_id,
                    const std::vector<base::FilePath>& warning_files,
                    const DlpFileDestination& destination,
                    dlp::FileAction action) {
        std::move(callback).Run(choice_result);
        return nullptr;
      });

  MockCheckIfDlpAllowedCallback cb;
  EXPECT_CALL(cb, Run(/*is_allowed=*/choice_result)).Times(1);

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, data_controls::Component::kUsb, _, _, _))
      .WillOnce(
          testing::DoAll(testing::SetArgPointee<3>(kExampleSourcePattern1),
                         testing::SetArgPointee<4>(kRuleMetadata1),
                         testing::Return(DlpRulesManager::Level::kWarn)));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  const base::FilePath file_path("MyUSB/path/in/removable/filename");

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "removable", file_path);
  ASSERT_TRUE(dst_url.is_valid());

  if (!choice_result) {
    EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                            /*task_id=*/{absl::nullopt},
                            std::vector<base::FilePath>{dst_url.path()},
                            dlp::FileAction::kDownload));
  }

  files_controller_->CheckIfDownloadAllowed(DlpFileDestination(kExampleUrl1),
                                            dst_url.path(), cb.Get());

  auto CreateEvent =
      [&](absl::optional<DlpRulesManager::Level> level) -> DlpPolicyEvent {
    auto event_builder =
        level.has_value()
            ? DlpPolicyEventBuilder::Event(
                  kExampleSourcePattern1, kRuleName1, kRuleId1,
                  DlpRulesManager::Restriction::kFiles, level.value())
            : DlpPolicyEventBuilder::WarningProceededEvent(
                  kExampleSourcePattern1, kRuleName1, kRuleId1,
                  DlpRulesManager::Restriction::kFiles);
    event_builder->SetDestinationComponent(data_controls::Component::kUsb);
    event_builder->SetContentName(file_path.BaseName().value());
    return event_builder->Create();
  };

  ASSERT_EQ(events.size(), 1u + (choice_result ? 1 : 0));
  EXPECT_THAT(events[0],
              IsDlpPolicyEvent(CreateEvent(DlpRulesManager::Level::kWarn)));
  if (choice_result) {
    EXPECT_THAT(events[1], IsDlpPolicyEvent(CreateEvent(absl::nullopt)));
  }

  EXPECT_THAT(
      histogram_tester.GetAllSamples(GetDlpHistogramPrefix() +
                                     std::string(dlp::kFileActionWarnedUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kDownload, 1),
                       base::Bucket(dlp::FileAction::kTransfer, 0)));

  EXPECT_THAT(
      histogram_tester.GetAllSamples(
          GetDlpHistogramPrefix() +
          std::string(dlp::kFileActionWarnProceededUMA)),
      base::BucketsAre(base::Bucket(dlp::FileAction::kDownload, choice_result),
                       base::Bucket(dlp::FileAction::kTransfer, 0)));

  storage::ExternalMountPoints::GetSystemInstance()->RevokeAllFileSystems();
}

class DlpFilesWarningDialogContentTest
    : public DlpFilesControllerAshTest,
      public ::testing::WithParamInterface<FilesTransferInfo> {};

INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesWarningDialogContentTest,
    ::testing::Values(
        FilesTransferInfo(policy::dlp::FileAction::kDownload,
                          std::vector<ino_t>({kInode1}),
                          std::vector<std::string>({kExampleUrl1}),
                          std::vector<std::string>({kFilePath1})),
        FilesTransferInfo(policy::dlp::FileAction::kTransfer,
                          std::vector<ino_t>({kInode1}),
                          std::vector<std::string>({kExampleUrl1}),
                          std::vector<std::string>({kFilePath1})),
        FilesTransferInfo(policy::dlp::FileAction::kTransfer,
                          std::vector<ino_t>({kInode1, kInode2}),
                          std::vector<std::string>({kExampleUrl1,
                                                    kExampleUrl2}),
                          std::vector<std::string>({kFilePath1, kFilePath2})),
        FilesTransferInfo(policy::dlp::FileAction::kUpload,
                          std::vector<ino_t>({kInode1}),
                          std::vector<std::string>({kExampleUrl1}),
                          std::vector<std::string>({kFilePath1})),
        FilesTransferInfo(policy::dlp::FileAction::kUpload,
                          std::vector<ino_t>({kInode1, kInode2}),
                          std::vector<std::string>({kExampleUrl1,
                                                    kExampleUrl2}),
                          std::vector<std::string>({kFilePath1, kFilePath2})),
        FilesTransferInfo(policy::dlp::FileAction::kCopy,
                          std::vector<ino_t>({kInode1}),
                          std::vector<std::string>({kExampleUrl1}),
                          std::vector<std::string>({kFilePath1})),
        FilesTransferInfo(policy::dlp::FileAction::kCopy,
                          std::vector<ino_t>({kInode1, kInode2}),
                          std::vector<std::string>({kExampleUrl1,
                                                    kExampleUrl2}),
                          std::vector<std::string>({kFilePath1, kFilePath2})),
        FilesTransferInfo(policy::dlp::FileAction::kMove,
                          std::vector<ino_t>({kInode1}),
                          std::vector<std::string>({kExampleUrl1}),
                          std::vector<std::string>({kFilePath1})),
        FilesTransferInfo(policy::dlp::FileAction::kMove,
                          std::vector<ino_t>({kInode1, kInode2}),
                          std::vector<std::string>({kExampleUrl1,
                                                    kExampleUrl2}),
                          std::vector<std::string>({kFilePath1, kFilePath2}))));

TEST_P(DlpFilesWarningDialogContentTest,
       IsFilesTransferRestricted_WarningDialogContent) {
  auto transfer_info = GetParam();
  std::vector<DlpFilesControllerAsh::FileDaemonInfo> warned_files;
  std::vector<
      std::pair<DlpFilesControllerAsh::FileDaemonInfo, ::dlp::RestrictionLevel>>
      files_levels;
  for (size_t i = 0; i < transfer_info.file_sources.size(); ++i) {
    DlpFilesControllerAsh::FileDaemonInfo file_info(
        transfer_info.file_inodes[i],
        base::FilePath(transfer_info.file_paths[i]),
        transfer_info.file_sources[i]);
    warned_files.emplace_back(file_info);
    files_levels.emplace_back(file_info,
                              ::dlp::RestrictionLevel::LEVEL_WARN_CANCEL);
  }
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  mount_points->RevokeAllFileSystems();
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      ash::kSystemMountNameRemovable, storage::kFileSystemTypeLocal,
      storage::FileSystemMountOption(),
      base::FilePath(file_manager::util::kRemovableMediaPath)));
  std::vector<FileDaemonInfo> files{
      FileDaemonInfo(kInode1, my_files_dir_.AppendASCII(kFilePath1),
                     kExampleUrl1),
      FileDaemonInfo(kInode2, my_files_dir_.AppendASCII(kFilePath2),
                     kExampleUrl2),
      FileDaemonInfo(kInode3, my_files_dir_.AppendASCII(kFilePath3),
                     kExampleUrl3)};
  std::vector<FileSystemURL> files_urls;
  AddFilesToDlpClient(std::move(files), files_urls);

  std::vector<base::FilePath> expected_files;

  for (const auto& file_path : transfer_info.file_paths) {
    expected_files.emplace_back(base::FilePath(file_path));
  }

  EXPECT_CALL(*rules_manager_,
              IsRestrictedComponent(_, data_controls::Component::kUsb, _, _, _))
      .WillRepeatedly(testing::Return(DlpRulesManager::Level::kWarn));

  EXPECT_CALL(*rules_manager_, GetReportingManager())
      .Times(::testing::AnyNumber());

  EXPECT_CALL(*fpnm_,
              ShowDlpWarning(base::test::IsNotNullCallback(), {absl::nullopt},
                             std::move(expected_files),
                             DlpFileDestination(data_controls::Component::kUsb),
                             transfer_info.files_action))
      .WillOnce([](OnDlpRestrictionCheckedCallback callback,
                   absl::optional<file_manager::io_task::IOTaskId> task_id,
                   const std::vector<base::FilePath>& confidential_files,
                   const DlpFileDestination& destination,
                   dlp::FileAction action) {
        std::move(callback).Run(false);
        return nullptr;
      });

  MockIsFilesTransferRestrictedCallback cb;
  EXPECT_CALL(cb, Run(files_levels)).Times(1);

  auto dst_url = mount_points->CreateExternalFileSystemURL(
      blink::StorageKey(), "removable",
      base::FilePath("MyUSB/path/in/removable"));
  ASSERT_TRUE(dst_url.is_valid());

  files_controller_->IsFilesTransferRestricted(
      /*task_id=*/absl::nullopt, warned_files,
      DlpFileDestination(dst_url.path().value()), transfer_info.files_action,
      cb.Get());

  storage::ExternalMountPoints::GetSystemInstance()->RevokeAllFileSystems();
}

class DlpFilesAppServiceTest : public DlpFilesControllerAshTest {
 public:
  DlpFilesAppServiceTest(const DlpFilesAppServiceTest&) = delete;
  DlpFilesAppServiceTest& operator=(const DlpFilesAppServiceTest&) = delete;

 protected:
  DlpFilesAppServiceTest() = default;

  ~DlpFilesAppServiceTest() override = default;

  void SetUp() override {
    DlpFilesControllerAshTest::SetUp();
    app_service_test_.SetUp(profile_.get());
    app_service_proxy_ =
        apps::AppServiceProxyFactory::GetForProfile(profile_.get());
    ASSERT_TRUE(app_service_proxy_);
  }

  void CreateAndStoreFakeApp(
      std::string fake_id,
      apps::AppType app_type,
      absl::optional<std::string> publisher_id = absl::nullopt) {
    std::vector<apps::AppPtr> fake_apps;
    apps::AppPtr fake_app = std::make_unique<apps::App>(app_type, fake_id);
    fake_app->name = "xyz";
    fake_app->show_in_management = true;
    fake_app->readiness = apps::Readiness::kReady;
    if (publisher_id.has_value()) {
      fake_app->publisher_id = publisher_id.value();
    }

    std::vector<apps::PermissionPtr> fake_permissions;
    fake_app->permissions = std::move(fake_permissions);

    fake_apps.push_back(std::move(fake_app));

    UpdateAppRegistryCache(std::move(fake_apps), app_type);
  }

  void UpdateAppRegistryCache(std::vector<apps::AppPtr> fake_apps,
                              apps::AppType app_type) {
    app_service_proxy_->AppRegistryCache().OnApps(
        std::move(fake_apps), app_type, /*should_notify_initialized=*/false);
  }

  raw_ptr<apps::AppServiceProxy, ExperimentalAsh> app_service_proxy_ = nullptr;
  apps::AppServiceTest app_service_test_;
};

TEST_F(DlpFilesAppServiceTest, CheckIfLaunchAllowed_ErrorResponse) {
  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.set_error_message("Did not receive a reply.");
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  CreateAndStoreFakeApp("arcApp", apps::AppType::kArc);

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionView);
  app_service_intent->mime_type = "*/*";
  const std::string path = "Documents/foo.txt";
  const std::string mime_type = "text/plain";
  auto url = ToGURL(base::FilePath(storage::kTestDir), path);
  EXPECT_TRUE(url.SchemeIsFileSystem());
  app_service_intent->files = std::vector<apps::IntentFilePtr>{};
  auto file = std::make_unique<apps::IntentFile>(url);
  file->mime_type = mime_type;
  app_service_intent->files.push_back(std::move(file));
  EXPECT_FALSE(app_service_intent->IsShareIntent());

  base::test::TestFuture<bool> launch_cb;
  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      kArcAppId,
      [this, &app_service_intent, &launch_cb](const apps::AppUpdate& update) {
        files_controller_->CheckIfLaunchAllowed(
            update, std::move(app_service_intent), launch_cb.GetCallback());
      }));
  EXPECT_EQ(launch_cb.Get(), true);

  auto last_check_files_transfer_request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  EXPECT_TRUE(last_check_files_transfer_request.has_file_action());
  EXPECT_EQ(last_check_files_transfer_request.file_action(),
            ::dlp::FileAction::OPEN);
  ASSERT_EQ(last_check_files_transfer_request.files_paths().size(), 1);
  EXPECT_EQ(last_check_files_transfer_request.files_paths()[0], path);
  EXPECT_EQ(last_check_files_transfer_request.destination_component(),
            ::dlp::DlpComponent::ARC);
  EXPECT_FALSE(last_check_files_transfer_request.has_io_task_id());
}

TEST_F(DlpFilesAppServiceTest, CheckIfLaunchAllowed_EmptyIntent) {
  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.set_error_message("Did not receive a reply.");
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  CreateAndStoreFakeApp("arcApp", apps::AppType::kArc);

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionView);

  base::test::TestFuture<bool> launch_cb;
  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      kArcAppId,
      [this, &app_service_intent, &launch_cb](const apps::AppUpdate& update) {
        files_controller_->CheckIfLaunchAllowed(
            update, std::move(app_service_intent), launch_cb.GetCallback());
      }));
  EXPECT_EQ(launch_cb.Get(), true);
}

TEST_F(DlpFilesAppServiceTest, IsLaunchBlocked_EmptyIntent) {
  CreateAndStoreFakeApp("arcApp", apps::AppType::kArc);

  ON_CALL(*rules_manager_, IsRestrictedComponent)
      .WillByDefault(testing::Return(DlpRulesManager::Level::kBlock));

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionView);

  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      kArcAppId, [this, &app_service_intent](const apps::AppUpdate& update) {
        EXPECT_FALSE(files_controller_->IsLaunchBlocked(
            update, std::move(app_service_intent)));
      }));
}

TEST_F(DlpFilesAppServiceTest, IsLaunchBlocked_NoSourceUrl) {
  CreateAndStoreFakeApp("arcApp", apps::AppType::kArc);

  ON_CALL(*rules_manager_, IsRestrictedComponent)
      .WillByDefault(testing::Return(DlpRulesManager::Level::kBlock));

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionSend);
  app_service_intent->mime_type = "*/*";
  app_service_intent->files = std::vector<apps::IntentFilePtr>{};
  auto url1 = ToGURL(base::FilePath(storage::kTestDir), "Documents/foo1.txt");
  EXPECT_TRUE(url1.SchemeIsFileSystem());
  auto file1 = std::make_unique<apps::IntentFile>(url1);
  file1->mime_type = "text/plain";
  app_service_intent->files.push_back(std::move(file1));

  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      kArcAppId, [this, &app_service_intent](const apps::AppUpdate& update) {
        EXPECT_FALSE(files_controller_->IsLaunchBlocked(
            update, std::move(app_service_intent)));
      }));
}

class DlpFilesAppLaunchTest : public DlpFilesAppServiceTest,
                              public ::testing::WithParamInterface<
                                  std::tuple<apps::AppType, std::string>> {
 public:
  DlpFilesAppLaunchTest(const DlpFilesAppServiceTest&) = delete;
  DlpFilesAppLaunchTest& operator=(const DlpFilesAppServiceTest&) = delete;

 protected:
  DlpFilesAppLaunchTest() = default;

  ~DlpFilesAppLaunchTest() override = default;

  void SetUp() override {
    DlpFilesAppServiceTest::SetUp();

    CreateAndStoreFakeApp(kChromeAppId, apps::AppType::kChromeApp,
                          kExampleUrl1);
    CreateAndStoreFakeApp(kArcAppId, apps::AppType::kArc, kExampleUrl2);
    CreateAndStoreFakeApp(kCrostiniAppId, apps::AppType::kCrostini,
                          kExampleUrl3);
    CreateAndStoreFakeApp(kPluginVmAppId, apps::AppType::kPluginVm,
                          kExampleUrl4);
    CreateAndStoreFakeApp(kWebAppId, apps::AppType::kWeb, kExampleUrl5);
  }
};

INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesAppLaunchTest,
    ::testing::Values(std::make_tuple(apps::AppType::kChromeApp, kChromeAppId),
                      std::make_tuple(apps::AppType::kArc, kArcAppId),
                      std::make_tuple(apps::AppType::kCrostini, kCrostiniAppId),
                      std::make_tuple(apps::AppType::kPluginVm, kPluginVmAppId),
                      std::make_tuple(apps::AppType::kWeb, kWebAppId)));

TEST_P(DlpFilesAppLaunchTest, CheckIfAppLaunchAllowed) {
  auto [app_type, app_id] = GetParam();

  const std::string path1 = "Documents/foo1.txt";
  const std::string path2 = "Documents/foo2.txt";

  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(path1);
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{absl::nullopt},
                          std::vector<base::FilePath>{base::FilePath(path1)},
                          dlp::FileAction::kOpen));

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionSend);
  app_service_intent->mime_type = "*/*";
  app_service_intent->files = std::vector<apps::IntentFilePtr>{};
  auto url1 = ToGURL(base::FilePath(storage::kTestDir), path1);
  EXPECT_TRUE(url1.SchemeIsFileSystem());
  auto file1 = std::make_unique<apps::IntentFile>(url1);
  file1->mime_type = "text/plain";
  app_service_intent->files.push_back(std::move(file1));
  auto url2 = ToGURL(base::FilePath(storage::kTestDir), path2);
  EXPECT_TRUE(url2.SchemeIsFileSystem());
  auto file2 = std::make_unique<apps::IntentFile>(url2);
  file2->mime_type = "text/plain";
  app_service_intent->files.push_back(std::move(file2));

  EXPECT_TRUE(app_service_intent->IsShareIntent());

  base::test::TestFuture<bool> launch_cb;
  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      app_id,
      [this, &app_service_intent, &launch_cb](const apps::AppUpdate& update) {
        files_controller_->CheckIfLaunchAllowed(
            update, std::move(app_service_intent), launch_cb.GetCallback());
      }));
  EXPECT_EQ(launch_cb.Get(), false);

  auto last_check_files_transfer_request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();

  if (app_type == apps::AppType::kChromeApp) {
    EXPECT_TRUE(last_check_files_transfer_request.has_destination_url());
    EXPECT_EQ(last_check_files_transfer_request.destination_url(),
              std::string(extensions::kExtensionScheme) + "://" + app_id);
  } else if (app_type == apps::AppType::kArc) {
    EXPECT_TRUE(last_check_files_transfer_request.has_destination_component());
    EXPECT_EQ(last_check_files_transfer_request.destination_component(),
              ::dlp::DlpComponent::ARC);

  } else if (app_type == apps::AppType::kCrostini) {
    EXPECT_TRUE(last_check_files_transfer_request.has_destination_component());
    EXPECT_EQ(last_check_files_transfer_request.destination_component(),
              ::dlp::DlpComponent::CROSTINI);

  } else if (app_type == apps::AppType::kPluginVm) {
    EXPECT_TRUE(last_check_files_transfer_request.has_destination_component());
    EXPECT_EQ(last_check_files_transfer_request.destination_component(),
              ::dlp::DlpComponent::PLUGIN_VM);

  } else if (app_type == apps::AppType::kWeb) {
    EXPECT_TRUE(last_check_files_transfer_request.has_destination_url());
    EXPECT_EQ(last_check_files_transfer_request.destination_url(),
              kExampleUrl5);
  }

  EXPECT_TRUE(last_check_files_transfer_request.has_file_action());
  EXPECT_EQ(last_check_files_transfer_request.file_action(),
            ::dlp::FileAction::SHARE);
  EXPECT_FALSE(last_check_files_transfer_request.has_io_task_id());

  std::vector<std::string> expected_requested_files;
  expected_requested_files.push_back(path1);
  expected_requested_files.push_back(path2);
  std::vector<std::string> requested_files(
      last_check_files_transfer_request.files_paths().begin(),
      last_check_files_transfer_request.files_paths().end());
  EXPECT_THAT(requested_files,
              testing::UnorderedElementsAreArray(expected_requested_files));
}

TEST_P(DlpFilesAppLaunchTest, IsLaunchBlocked) {
  auto [app_type, app_id] = GetParam();

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionSend);
  app_service_intent->mime_type = "*/*";
  app_service_intent->files = std::vector<apps::IntentFilePtr>{};
  auto url1 = ToGURL(base::FilePath(storage::kTestDir), "Documents/foo1.txt");
  EXPECT_TRUE(url1.SchemeIsFileSystem());
  auto file1 = std::make_unique<apps::IntentFile>(url1);
  file1->mime_type = "text/plain";
  file1->dlp_source_url = kExampleUrl1;
  app_service_intent->files.push_back(std::move(file1));

  std::vector<std::string> urls;
  urls.push_back(kExampleUrl1);

  if (app_type == apps::AppType::kChromeApp ||
      app_type == apps::AppType::kWeb) {
    EXPECT_CALL(*rules_manager_, IsRestrictedDestination)
        .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  } else {
    EXPECT_CALL(*rules_manager_, IsRestrictedComponent)
        .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  }

  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      app_id, [this, &app_service_intent](const apps::AppUpdate& update) {
        EXPECT_TRUE(files_controller_->IsLaunchBlocked(
            update, std::move(app_service_intent)));
      }));
}

TEST_P(DlpFilesAppLaunchTest, IsLaunchBlocked_Empty) {
  auto [app_type, app_id] = GetParam();

  auto app_service_intent =
      std::make_unique<apps::Intent>(apps_util::kIntentActionSend);
  app_service_intent->mime_type = "*/*";
  app_service_intent->files = std::vector<apps::IntentFilePtr>{};
  auto url1 = ToGURL(base::FilePath(storage::kTestDir), "Documents/foo1.txt");
  EXPECT_TRUE(url1.SchemeIsFileSystem());
  auto file1 = std::make_unique<apps::IntentFile>(url1);
  file1->mime_type = "text/plain";
  file1->dlp_source_url = kExampleUrl1;
  app_service_intent->files.push_back(std::move(file1));

  if (app_type == apps::AppType::kChromeApp ||
      app_type == apps::AppType::kWeb) {
    EXPECT_CALL(*rules_manager_, IsRestrictedDestination)
        .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  } else {
    EXPECT_CALL(*rules_manager_, IsRestrictedComponent)
        .WillOnce(testing::Return(DlpRulesManager::Level::kBlock));
  }

  ASSERT_TRUE(files_controller_);
  EXPECT_TRUE(app_service_proxy_->AppRegistryCache().ForOneApp(
      app_id, [this, &app_service_intent](const apps::AppUpdate& update) {
        EXPECT_TRUE(files_controller_->IsLaunchBlocked(
            update, std::move(app_service_intent)));
      }));
}

class DlpFilesDnDTest
    : public DlpFilesControllerAshTest,
      public ::testing::WithParamInterface<
          std::pair<ui::DataTransferEndpoint, ::dlp::DlpComponent>> {
 public:
  DlpFilesDnDTest(const DlpFilesDnDTest&) = delete;
  DlpFilesDnDTest& operator=(const DlpFilesDnDTest&) = delete;

 protected:
  DlpFilesDnDTest() = default;

  ~DlpFilesDnDTest() override = default;
};

INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesDnDTest,
    ::testing::Values(
        std::make_tuple(ui::DataTransferEndpoint(ui::EndpointType::kArc),
                        ::dlp::DlpComponent::ARC),
        std::make_tuple(ui::DataTransferEndpoint(ui::EndpointType::kCrostini),
                        ::dlp::DlpComponent::CROSTINI),
        std::make_tuple(ui::DataTransferEndpoint(ui::EndpointType::kPluginVm),
                        ::dlp::DlpComponent::PLUGIN_VM)));

// Tests dropping a mix of an external file and a local directory.
TEST_P(DlpFilesDnDTest, CheckIfDropAllowed) {
  storage::ExternalMountPoints* mount_points =
      storage::ExternalMountPoints::GetSystemInstance();
  ASSERT_TRUE(mount_points);
  ASSERT_TRUE(mount_points->RegisterFileSystem(
      "c", storage::kFileSystemTypeLocal, storage::FileSystemMountOption(),
      my_files_dir_));
  base::ScopedClosureRunner external_mount_points_revoker(
      base::BindOnce(&storage::ExternalMountPoints::RevokeAllFileSystems,
                     base::Unretained(mount_points)));

  base::ScopedTempDir sub_dir1;
  ASSERT_TRUE(sub_dir1.CreateUniqueTempDirUnderPath(my_files_dir_));
  base::FilePath file_path1 = sub_dir1.GetPath().AppendASCII(kFilePath1);
  ASSERT_TRUE(CreateDummyFile(file_path1));
  auto file_url1 = CreateFileSystemURL(file_path1.value());

  // Set CheckFilesTransfer response to restrict the local file.
  ::dlp::CheckFilesTransferResponse check_files_transfer_response;
  check_files_transfer_response.add_files_paths(file_path1.value());
  ASSERT_TRUE(chromeos::DlpClient::Get()->IsAlive());
  chromeos::DlpClient::Get()->GetTestInterface()->SetCheckFilesTransferResponse(
      check_files_transfer_response);

  EXPECT_CALL(*fpnm_, ShowDlpBlockedFiles(
                          /*task_id=*/{absl::nullopt},
                          std::vector<base::FilePath>{file_path1},
                          dlp::FileAction::kMove));

  std::vector<ui::FileInfo> dropped_files{ui::FileInfo(file_path1, file_path1)};
  auto [data_dst, expected_component] = GetParam();

  base::test::TestFuture<bool> future;
  ASSERT_TRUE(files_controller_);
  files_controller_->CheckIfDropAllowed(dropped_files, &data_dst,
                                        future.GetCallback());
  EXPECT_TRUE(future.Wait());
  EXPECT_EQ(false, future.Take());

  // Validate that only the local file was sent to the daemon.
  ::dlp::CheckFilesTransferRequest request =
      chromeos::DlpClient::Get()
          ->GetTestInterface()
          ->GetLastCheckFilesTransferRequest();
  ASSERT_EQ(request.files_paths().size(), 1);
  EXPECT_EQ(request.files_paths()[0], file_path1.value());
  ASSERT_TRUE(request.has_destination_component());
  EXPECT_EQ(request.destination_component(), expected_component);
  EXPECT_FALSE(request.has_io_task_id());
}

class DlpFilesControllerAshComponentsTest
    : public DlpFilesTestWithMounts,
      public ::testing::WithParamInterface<
          std::tuple<std::string,
                     std::string,
                     absl::optional<data_controls::Component>>> {
 public:
  DlpFilesControllerAshComponentsTest(
      const DlpFilesControllerAshComponentsTest&) = delete;
  DlpFilesControllerAshComponentsTest& operator=(
      const DlpFilesControllerAshComponentsTest&) = delete;

 protected:
  DlpFilesControllerAshComponentsTest() = default;
  ~DlpFilesControllerAshComponentsTest() = default;
};

INSTANTIATE_TEST_SUITE_P(
    DlpFiles,
    DlpFilesControllerAshComponentsTest,
    ::testing::Values(
        std::make_tuple("android_files",
                        "path/in/android",
                        data_controls::Component::kArc),
        std::make_tuple("removable",
                        "MyUSB/path/in/removable",
                        data_controls::Component::kUsb),
        std::make_tuple("crostini_test_termina_penguin",
                        "path/in/crostini",
                        data_controls::Component::kCrostini),
        std::make_tuple("drivefs-84675c855b63e12f384d45f033826980",
                        "root/path/in/mydrive",
                        data_controls::Component::kDrive),
        std::make_tuple("", "/Downloads", absl::nullopt)));

TEST_P(DlpFilesControllerAshComponentsTest, MapFilePathtoPolicyComponentTest) {
  auto [mount_name, path, expected_component] = GetParam();
  auto url = mount_points_->CreateExternalFileSystemURL(
      blink::StorageKey(), mount_name, base::FilePath(path));
  EXPECT_EQ(files_controller_->MapFilePathtoPolicyComponent(profile_.get(),
                                                            url.path()),
            expected_component);
}

}  // namespace policy
