// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {PerformanceTestRunner} from 'performance_test_runner';

(async function() {
  TestRunner.addResult(`Test recording page reload works in Timeline.\n`);
  await TestRunner.loadLegacyModule('timeline');
  await TestRunner.showPanel('timeline');
  TestRunner.resourceTreeModel.addEventListener(SDK.ResourceTreeModel.Events.Load, TestRunner.pageLoaded);

  var panel = UI.panels.timeline;
  PerformanceTestRunner.runWhenTimelineIsReady(recordingStopped);

  panel.millisecondsToRecordAfterLoadEvent = 1;
  panel.recordReload();

  function recordingStopped() {
    TestRunner.addResult('Recording stopped');
    TestRunner.completeTest();
  }
})();
