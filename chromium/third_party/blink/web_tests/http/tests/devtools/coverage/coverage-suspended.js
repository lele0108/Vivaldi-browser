// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {CoverageTestRunner} from 'coverage_test_runner';

(async function() {
  TestRunner.addResult(`Tests the coverage list view after suspending the coverage model.\n`);
  await TestRunner.loadLegacyModule('panels/coverage');
  await TestRunner.loadHTML(`
      <p class="class">
      </p>
    `);
  await TestRunner.addStylesheetTag('resources/highlight-in-source.css');

  await CoverageTestRunner.startCoverage(true);
  await CoverageTestRunner.suspendCoverageModel();
  await TestRunner.addScriptTag('resources/coverage.js');
  await TestRunner.evaluateInPagePromise('performActions()');
  await CoverageTestRunner.resumeCoverageModel();
  await CoverageTestRunner.stopCoverage();
  TestRunner.addResult('Initial');
  CoverageTestRunner.dumpCoverageListView();

  await CoverageTestRunner.startCoverage(true);
  await CoverageTestRunner.suspendCoverageModel();
  await CoverageTestRunner.resumeCoverageModel();
  await CoverageTestRunner.stopCoverage();
  TestRunner.addResult('After second session');
  CoverageTestRunner.dumpCoverageListView();

  await CoverageTestRunner.suspendCoverageModel();
  await CoverageTestRunner.resumeCoverageModel();

  var coverageView = Coverage.CoverageView.instance();
  coverageView.clear();

  await CoverageTestRunner.startCoverage(true);
  await TestRunner.evaluateInPagePromise('performActions()');
  await CoverageTestRunner.stopCoverage();
  TestRunner.addResult('After clear');
  CoverageTestRunner.dumpCoverageListView();

  TestRunner.completeTest();
})();
