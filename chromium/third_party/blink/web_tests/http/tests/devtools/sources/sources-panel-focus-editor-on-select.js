// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {SourcesTestRunner} from 'sources_test_runner';

(async function() {
  TestRunner.addResult(`Verifies that text editor has focus after panel re-selecting.\n`);
  await TestRunner.loadLegacyModule('sources');
  await TestRunner.showPanel('sources');

  await TestRunner.addScriptTag('resources/script.js');

  SourcesTestRunner.showScriptSource('script.js', onSourceFrame);
  function onSourceFrame(sourceFrame) {
    TestRunner.addResult('initial: focused = ' + sourceFrame.hasFocus());
    UI.inspectorView.showPanel('elements')
        .then(() => UI.inspectorView.showPanel('sources'))
        .then(onPanelReselected.bind(null, sourceFrame));
  }

  function onPanelReselected(sourceFrame) {
    TestRunner.addResult('after panel reselected: focused = ' + sourceFrame.hasFocus());
    TestRunner.completeTest();
  }
})();
