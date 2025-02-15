// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.net;

import static com.google.common.truth.Truth.assertThat;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SmallTest;

import org.junit.After;
import org.junit.Before;
import org.junit.Rule;
import org.junit.Test;
import org.junit.runner.RunWith;

import org.chromium.net.CronetTestRule.OnlyRunNativeCronet;

/**
 * Unit tests for {@code MockCertVerifier}.
 */
@RunWith(AndroidJUnit4.class)
public class MockCertVerifierTest {
    @Rule
    public final CronetTestRule mTestRule = CronetTestRule.withManualEngineStartup();

    @Before
    public void setUp() throws Exception {
        // Load library first to create MockCertVerifier.
        System.loadLibrary("cronet_tests");

        assertThat(Http2TestServer.startHttp2TestServer(mTestRule.getTestFramework().getContext()))
                .isTrue();
    }

    @After
    public void tearDown() throws Exception {
        assertThat(Http2TestServer.shutdownHttp2TestServer()).isTrue();
    }

    @Test
    @SmallTest
    @OnlyRunNativeCronet
    public void testRequest_failsWithoutMockVerifier() {
        String url = Http2TestServer.getEchoAllHeadersUrl();
        TestUrlRequestCallback callback = startAndWaitForComplete(url);
        assertThat(callback.mError).isNotNull();
        assertThat(callback.mError).hasMessageThat().contains("ERR_CERT_AUTHORITY_INVALID");
    }

    @Test
    @SmallTest
    @OnlyRunNativeCronet
    public void testRequest_passesWithMockVerifier() {
        mTestRule.getTestFramework().applyEngineBuilderPatch(
                (builder)
                        -> CronetTestUtil.setMockCertVerifierForTesting(
                                builder, MockCertVerifier.createFreeForAllMockCertVerifier()));

        String url = Http2TestServer.getEchoAllHeadersUrl();
        TestUrlRequestCallback callback = startAndWaitForComplete(url);
        assertThat(callback.mResponseInfo.getHttpStatusCode()).isEqualTo(200);
    }

    private TestUrlRequestCallback startAndWaitForComplete(String url) {
        TestUrlRequestCallback callback = new TestUrlRequestCallback();
        UrlRequest.Builder builder =
                mTestRule.getTestFramework().startEngine().newUrlRequestBuilder(
                        url, callback, callback.getExecutor());
        builder.build().start();
        callback.blockForDone();
        return callback;
    }
}
