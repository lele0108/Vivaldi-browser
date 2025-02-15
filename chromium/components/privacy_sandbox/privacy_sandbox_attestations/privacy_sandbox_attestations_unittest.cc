// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "components/privacy_sandbox/privacy_sandbox_attestations/privacy_sandbox_attestations.h"

#include "base/feature_list.h"
#include "base/files/file_path.h"
#include "base/files/file_util.h"
#include "base/files/scoped_temp_dir.h"
#include "base/strings/string_piece.h"
#include "base/test/scoped_feature_list.h"
#include "base/version.h"
#include "components/privacy_sandbox/privacy_sandbox_attestations/proto/privacy_sandbox_attestations.pb.h"
#include "components/privacy_sandbox/privacy_sandbox_attestations/scoped_privacy_sandbox_attestations.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "content/public/test/browser_task_environment.h"
#include "net/base/schemeful_site.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "url/gurl.h"

namespace privacy_sandbox {

class PrivacySandboxAttestationsTestBase : public testing::Test {
 public:
  PrivacySandboxAttestationsTestBase()
      : scoped_attestations_(PrivacySandboxAttestations::CreateForTesting()) {}

 protected:
  using Status = PrivacySandboxSettingsImpl::Status;

 private:
  content::BrowserTaskEnvironment browser_task_environment_;
  ScopedPrivacySandboxAttestations scoped_attestations_;
};

TEST_F(PrivacySandboxAttestationsTestBase, AddOverride) {
  net::SchemefulSite site(GURL("https://example.com"));
  ASSERT_FALSE(PrivacySandboxAttestations::GetInstance()->IsOverridden(site));

  PrivacySandboxAttestations::GetInstance()->AddOverride(site);
  EXPECT_TRUE(PrivacySandboxAttestations::GetInstance()->IsOverridden(site));
}

TEST_F(PrivacySandboxAttestationsTestBase,
       SiteDefaultAttestedWithFeatureDefaultDisabled) {
  // Enrollment feature should be disabled by default.
  ASSERT_FALSE(base::FeatureList::IsEnabled(
      privacy_sandbox::kEnforcePrivacySandboxAttestations));
  net::SchemefulSite site(GURL("https://example.com"));

  Status attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  EXPECT_EQ(attestation_status, Status::kAllowed);
}

class PrivacySandboxAttestationsFeatureEnabledTest
    : public PrivacySandboxAttestationsTestBase {
 public:
  PrivacySandboxAttestationsFeatureEnabledTest() {
    scoped_feature_list_.InitAndEnableFeature(
        privacy_sandbox::kEnforcePrivacySandboxAttestations);
  }

  void SetUp() override { ASSERT_TRUE(scoped_temp_dir_.CreateUniqueTempDir()); }

  void WriteAttestationsFileAndWaitForLoading(base::Version version,
                                              base::StringPiece content) {
    base::ScopedTempDir component_install_dir;
    CHECK(component_install_dir.CreateUniqueTempDirUnderPath(
        scoped_temp_dir_.GetPath()));
    base::FilePath attestations_file_path =
        component_install_dir.GetPath().Append(
            FILE_PATH_LITERAL("attestations"));
    CHECK(base::WriteFile(attestations_file_path, content));

    base::RunLoop run_loop;
    privacy_sandbox::PrivacySandboxAttestations::GetInstance()
        ->SetLoadAttestationsDoneCallbackForTesting(run_loop.QuitClosure());

    PrivacySandboxAttestations::GetInstance()->LoadAttestations(
        version, attestations_file_path);
    run_loop.Run();
  }

 private:
  base::ScopedTempDir scoped_temp_dir_;
  base::test::ScopedFeatureList scoped_feature_list_;
};

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       DefaultDenyIfAttestationsMapNotPresent) {
  net::SchemefulSite site(GURL("https://example.com"));

  Status attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  EXPECT_EQ(attestation_status, Status::kAttestationsNotLoaded);
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest, AttestedIfOverridden) {
  net::SchemefulSite site(GURL("https://example.com"));
  Status attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  ASSERT_NE(attestation_status, Status::kAllowed);

  PrivacySandboxAttestations::GetInstance()->AddOverride(site);
  EXPECT_TRUE(PrivacySandboxAttestations::GetInstance()->IsOverridden(site));
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       EnrolledWithoutAttestations) {
  net::SchemefulSite site(GURL("https://example.com"));
  Status attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  ASSERT_NE(attestation_status, Status::kAllowed);

  PrivacySandboxAttestations::GetInstance()->SetAttestationsForTesting(
      PrivacySandboxAttestationsMap{{site, {}}});

  Status new_attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  EXPECT_NE(new_attestation_status, Status::kAllowed);
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest, EnrolledAndAttested) {
  net::SchemefulSite site(GURL("https://example.com"));
  Status attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  ASSERT_NE(attestation_status, Status::kAllowed);

  PrivacySandboxAttestations::GetInstance()->SetAttestationsForTesting(
      PrivacySandboxAttestationsMap{
          {site, PrivacySandboxAttestationsGatedAPISet{
                     PrivacySandboxAttestationsGatedAPI::kTopics}}});

  Status new_attestation_status =
      PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
          site, PrivacySandboxAttestationsGatedAPI::kTopics);
  EXPECT_EQ(new_attestation_status, Status::kAllowed);
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       NonExistentAttestationsFile) {
  base::RunLoop run_loop;
  privacy_sandbox::PrivacySandboxAttestations::GetInstance()
      ->SetLoadAttestationsDoneCallbackForTesting(run_loop.QuitClosure());

  // Call the parsing function with a non-existent file.
  PrivacySandboxAttestations::GetInstance()->LoadAttestations(
      base::Version("0.0.1"), base::FilePath());
  run_loop.Run();

  // The parsing should fail.
  EXPECT_FALSE(PrivacySandboxAttestations::GetInstance()
                   ->GetVersionForTesting()
                   .IsValid());
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       InvalidAttestationsFileIsNotLoaded) {
  // Write an invalid proto file, and try to parse it.
  WriteAttestationsFileAndWaitForLoading(base::Version("0.0.1"),
                                         "invalid proto");

  // The parsing should fail.
  EXPECT_FALSE(PrivacySandboxAttestations::GetInstance()
                   ->GetVersionForTesting()
                   .IsValid());
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest, LoadAttestationsFile) {
  PrivacySandboxAttestationsProto proto;
  ASSERT_TRUE(proto.site_attestations_size() == 0);

  std::string site = "https://example.com";
  ASSERT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAttestationsNotLoaded);

  // Add attestation for the site.
  PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto
      site_attestation;
  site_attestation.add_attested_apis(TOPICS);
  (*proto.mutable_site_attestations())[site] = site_attestation;

  std::string serialized_proto;
  proto.SerializeToString(&serialized_proto);

  WriteAttestationsFileAndWaitForLoading(base::Version("0.0.1"),
                                         serialized_proto);

  // The site should be attested for the API.
  ASSERT_TRUE(PrivacySandboxAttestations::GetInstance()
                  ->GetVersionForTesting()
                  .IsValid());
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->GetVersionForTesting(),
            base::Version("0.0.1"));
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAllowed);
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       OlderVersionAttestationsFileIsNotLoaded) {
  PrivacySandboxAttestationsProto proto;
  ASSERT_TRUE(proto.site_attestations_size() == 0);

  std::string site = "https://example.com";
  ASSERT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAttestationsNotLoaded);

  // Add attestation for the site.
  PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto
      site_attestation;
  site_attestation.add_attested_apis(TOPICS);
  (*proto.mutable_site_attestations())[site] = site_attestation;

  std::string serialized_proto;
  proto.SerializeToString(&serialized_proto);

  WriteAttestationsFileAndWaitForLoading(base::Version("1.2.3"),
                                         serialized_proto);

  // The site should be attested for the API.
  ASSERT_TRUE(PrivacySandboxAttestations::GetInstance()
                  ->GetVersionForTesting()
                  .IsValid());
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->GetVersionForTesting(),
            base::Version("1.2.3"));
  ASSERT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAllowed);

  // Clear the proto attestations.
  proto.clear_site_attestations();

  // Load the attestations file, which has an older version.
  proto.SerializeToString(&serialized_proto);
  WriteAttestationsFileAndWaitForLoading(base::Version("0.0.1"),
                                         serialized_proto);

  // The attestations map should still be the old one.
  ASSERT_TRUE(PrivacySandboxAttestations::GetInstance()
                  ->GetVersionForTesting()
                  .IsValid());
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->GetVersionForTesting(),
            base::Version("1.2.3"));
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAllowed);
}

TEST_F(PrivacySandboxAttestationsFeatureEnabledTest,
       NewerVersionAttestationsFileIsLoaded) {
  PrivacySandboxAttestationsProto proto;
  ASSERT_TRUE(proto.site_attestations_size() == 0);

  std::string site = "https://example.com";
  ASSERT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAttestationsNotLoaded);

  // Add attestation for the site.
  PrivacySandboxAttestationsProto::PrivacySandboxAttestedAPIsProto
      site_attestation;
  site_attestation.add_attested_apis(TOPICS);
  (*proto.mutable_site_attestations())[site] = site_attestation;

  std::string serialized_proto;
  proto.SerializeToString(&serialized_proto);
  WriteAttestationsFileAndWaitForLoading(base::Version("0.0.1"),
                                         serialized_proto);

  // The site should be attested for the API.
  ASSERT_TRUE(PrivacySandboxAttestations::GetInstance()
                  ->GetVersionForTesting()
                  .IsValid());
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->GetVersionForTesting(),
            base::Version("0.0.1"));
  ASSERT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAllowed);

  // Clear the attestations.
  proto.clear_site_attestations();

  // Load the attestations file, which has a newer version.
  proto.SerializeToString(&serialized_proto);
  WriteAttestationsFileAndWaitForLoading(base::Version("0.0.2"),
                                         serialized_proto);

  // The newer version should override the existing attestations map.
  ASSERT_TRUE(PrivacySandboxAttestations::GetInstance()
                  ->GetVersionForTesting()
                  .IsValid());
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->GetVersionForTesting(),
            base::Version("0.0.2"));

  // The site should not be attested for the API according to the new
  // attestations map.
  EXPECT_EQ(PrivacySandboxAttestations::GetInstance()->IsSiteAttested(
                net::SchemefulSite(GURL(site)),
                PrivacySandboxAttestationsGatedAPI::kTopics),
            Status::kAttestationFailed);
}

}  // namespace privacy_sandbox
