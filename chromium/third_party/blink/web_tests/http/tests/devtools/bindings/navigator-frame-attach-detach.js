// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {SourcesTestRunner} from 'sources_test_runner';
import {BindingsTestRunner} from 'bindings_test_runner';

(async function () {
  TestRunner.addResult(`Verify that navigator is rendered properly when frames come and go.\n`);
  await TestRunner.loadLegacyModule('sources');

  var sourcesNavigator = new Sources.NetworkNavigatorView();
  sourcesNavigator.show(UI.inspectorView.element);

  TestRunner.markStep('dumpInitialNavigator');
  SourcesTestRunner.dumpNavigatorView(sourcesNavigator, false);

  TestRunner.markStep('attachFrame');
  await BindingsTestRunner.attachFrame('frame', './resources/magic-frame.html');
  SourcesTestRunner.dumpNavigatorView(sourcesNavigator, false);

  TestRunner.markStep('detachFrame');
  await BindingsTestRunner.detachFrame('frame');
  SourcesTestRunner.dumpNavigatorView(sourcesNavigator, false);

  TestRunner.completeTest();
})();
