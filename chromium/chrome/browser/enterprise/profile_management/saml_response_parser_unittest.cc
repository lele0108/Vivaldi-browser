// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/enterprise/profile_management/saml_response_parser.h"

#include <string>
#include <vector>

#include "base/base64.h"
#include "base/containers/flat_map.h"
#include "base/run_loop.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "chrome/test/base/browser_with_test_window_test.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace profile_management {

using testing::_;

namespace {
const char kValidSAMLResponse[] = R"(
<samlp:Response ID="id" Version="2.0">
  <Issuer xmlns="urn:oasis:names:tc:SAML:2.0">https://issuer.com/</Issuer>
  <samlp:Status>
    <samlp:StatusCode Value="urn:oasis:names:tc:SAML:2.0:status:Success"/>
  </samlp:Status>
  <Assertion ID="session" Version="2.0" xmlns="urn:oasis:names:tc:SAML:2.0">
    <Issuer>https://issuer.com/</Issuer>
    <Subject>
      <NameID Format="urn:oasis:names:tc:SAML:1.1:nameid-format:emailAddress">
user@domain.com
      </NameID>
    </Subject>
    <AttributeStatement>
      <Attribute Name="attribute">
        <AttributeValue>attributevalue</AttributeValue>
      </Attribute>
      <Attribute Name="anotherattribute">
        <AttributeValue>anotherattributevalue</AttributeValue>
      </Attribute>
      <Attribute Name="uselessattribute">
        <AttributeValue>uselessattributevalue</AttributeValue>
      </Attribute>
    </AttributeStatement>
  </Assertion>
</samlp:Response>)";

const char kHTMLTemplate[] = R"(
<html>
  <body>
    <form method="POST" name="hiddenform">
      <input type="hidden" name="SAMLResponse" value="%s"/>
    </form>
  </body>
</html>)";

}  // namespace

class SAMLResponseParserTest : public BrowserWithTestWindowTest {
 public:
  SAMLResponseParserTest() = default;

  ~SAMLResponseParserTest() override = default;

  void SetUp() override { BrowserWithTestWindowTest::SetUp(); }

 protected:
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
  mojo::ScopedDataPipeProducerHandle producer_handle_;
  mojo::ScopedDataPipeConsumerHandle consumer_handle_;
};

TEST_F(SAMLResponseParserTest, RetrievesNoAttributesWithEmptyResponse) {
  std::string response = "<html></html>";
  uint32_t write_size = response.size();
  ASSERT_EQ(
      mojo::CreateDataPipe(response.size(), producer_handle_, consumer_handle_),
      MOJO_RESULT_OK);
  base::flat_map<std::string, std::string> attributes;

  base::RunLoop loop;
  SAMLResponseParser body_reader(
      std::vector<std::string>{"attribute", "notattribute", "anotherattribute"},
      consumer_handle_.get(),
      base::BindLambdaForTesting(
          [&attributes,
           &loop](base::flat_map<std::string, std::string> result) {
            attributes = std::move(result);
            loop.Quit();
          }));
  ASSERT_EQ(producer_handle_->WriteData(response.c_str(), &write_size,
                                        MOJO_WRITE_DATA_FLAG_NONE),
            MOJO_RESULT_OK);
  loop.Run();

  EXPECT_TRUE(attributes.empty());
}

TEST_F(SAMLResponseParserTest, RetrievesNoAttributesWithEmptySAMLResponse) {
  std::string response = base::StringPrintf(kHTMLTemplate, "");
  uint32_t write_size = response.size();
  ASSERT_EQ(
      mojo::CreateDataPipe(response.size(), producer_handle_, consumer_handle_),
      MOJO_RESULT_OK);
  base::flat_map<std::string, std::string> attributes;

  base::RunLoop loop;
  SAMLResponseParser body_reader(
      std::vector<std::string>{"attribute", "notattribute", "anotherattribute"},
      consumer_handle_.get(),
      base::BindLambdaForTesting(
          [&attributes,
           &loop](base::flat_map<std::string, std::string> result) {
            attributes = std::move(result);
            loop.Quit();
          }));
  ASSERT_EQ(producer_handle_->WriteData(response.c_str(), &write_size,
                                        MOJO_WRITE_DATA_FLAG_NONE),
            MOJO_RESULT_OK);
  loop.Run();

  EXPECT_TRUE(attributes.empty());
}

TEST_F(SAMLResponseParserTest, RetrievesSpecifiedAttributesWithValidResponse) {
  std::string encoded_saml_response;
  base::Base64Encode(kValidSAMLResponse, &encoded_saml_response);
  std::string response =
      base::StringPrintf(kHTMLTemplate, encoded_saml_response.c_str());
  uint32_t write_size = response.size();
  ASSERT_EQ(
      mojo::CreateDataPipe(response.size(), producer_handle_, consumer_handle_),
      MOJO_RESULT_OK);
  base::flat_map<std::string, std::string> attributes;

  base::RunLoop loop;
  SAMLResponseParser body_reader(
      std::vector<std::string>{"attribute", "notattribute", "anotherattribute"},
      consumer_handle_.get(),
      base::BindLambdaForTesting(
          [&attributes,
           &loop](base::flat_map<std::string, std::string> result) {
            attributes = std::move(result);
            loop.Quit();
          }));
  ASSERT_EQ(producer_handle_->WriteData(response.c_str(), &write_size,
                                        MOJO_WRITE_DATA_FLAG_NONE),
            MOJO_RESULT_OK);
  loop.Run();

  EXPECT_EQ(attributes["attribute"], "attributevalue");
  EXPECT_EQ(attributes["anotherattribute"], "anotherattributevalue");
  EXPECT_EQ(attributes.find("notattribute"), attributes.end());
  EXPECT_EQ(attributes.find("uselessattribute"), attributes.end());
}

}  // namespace profile_management
