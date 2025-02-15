// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {AxeCoreTestRunner} from 'axe_core_test_runner';
import {BindingsTestRunner} from 'bindings_test_runner';
import {SourcesTestRunner} from 'sources_test_runner';

(async function() {
  TestRunner.addResult('Tests accessibility in the Sources panel Navigator pane FileSystem tab using axe-core.');

  // axe-core issue #1444 -- role="tree" requires children with role="treeitem",
  // but it is reasonable to have trees with no leaves.
  const NO_REQUIRED_CHILDREN_RULESET = {
    'aria-required-children': {
      enabled: false,
    },
  };


  await UI.viewManager.showView('sources');
  await setup();

  await testA11yForView(NO_REQUIRED_CHILDREN_RULESET);

  TestRunner.completeTest();

  async function setup() {
    // Add a filesystem folder
    const testFileSystem = new BindingsTestRunner.TestFileSystem('file:///var/www3');
    await testFileSystem.reportCreatedPromise();
  }

  async function testA11yForView(ruleSet) {
    await UI.viewManager.showView('navigator-files');
    const sourcesNavigatorView = new Sources.FilesNavigatorView();

    sourcesNavigatorView.show(UI.inspectorView.element);
    SourcesTestRunner.dumpNavigatorView(sourcesNavigatorView);
    const element = UI.panels.sources.navigatorTabbedLocation.tabbedPane().element;
    await AxeCoreTestRunner.runValidation(element, ruleSet);
  }
})();
