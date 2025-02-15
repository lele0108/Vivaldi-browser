// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {NetworkTestRunner} from 'network_test_runner';
import {ConsoleTestRunner} from 'console_test_runner';
(async function() {
  TestRunner.addResult('Tests the signed exchange information are available when the certificate file is not available.\n');
  await TestRunner.loadLegacyModule('console');
  await TestRunner.showPanel('network');
  NetworkTestRunner.networkLog().reset();
  await TestRunner.addIframe('/loading/sxg/resources/sxg-cert-not-found.sxg');
  await ConsoleTestRunner.dumpConsoleMessages();
  NetworkTestRunner.dumpNetworkRequestsWithSignedExchangeInfo();
  TestRunner.completeTest();
})();
