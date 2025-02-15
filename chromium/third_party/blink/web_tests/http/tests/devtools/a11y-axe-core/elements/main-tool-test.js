// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {AxeCoreTestRunner} from 'axe_core_test_runner';
import {ElementsTestRunner} from 'elements_test_runner';

(async function() {
  // axe-core issue #1444 -- role="tree" requires children with role="treeitem",
  // but it is reasonable to have trees with no leaves.
  const NO_REQUIRED_CHILDREN_RULESET = {
    'aria-required-children': {
      enabled: false,
    },
  };
  const DEFAULT_RULESET = {};
  const tests = [
    testElementsDomTree,
    testElementsDomBreadcrumbs,
    testElementsStylesPane,
    testElementsComputedStylesPane,
  ];

  async function testElementsDomTree() {
    TestRunner.addResult('Tests accessibility in the DOM tree using the axe-core linter');
    const view = 'elements';
    await UI.viewManager.showView(view);
    const widget = await UI.viewManager.view(view).widget();
    const element = widget.element.querySelector('#elements-content');

    await AxeCoreTestRunner.runValidation(element, NO_REQUIRED_CHILDREN_RULESET);
  }

  async function testElementsDomBreadcrumbs() {
    TestRunner.addResult('Tests accessibility in the DOM breadcrumbs using the axe-core linter');
    const view = 'elements';
    await UI.viewManager.showView(view);
    const widget = await UI.viewManager.view(view).widget();
    const element = widget.element.querySelector('#elements-crumbs');

    await AxeCoreTestRunner.runValidation(element, DEFAULT_RULESET);
  }

  async function testElementsStylesPane() {
    TestRunner.addResult('Tests accessibility of the Styles pane using the axe-core linter');
    await UI.viewManager.showView('elements');
    const panel = Elements.ElementsPanel.instance();
    const element = panel.stylesWidget.element;

    await AxeCoreTestRunner.runValidation(element, NO_REQUIRED_CHILDREN_RULESET);
  }

  async function testElementsComputedStylesPane() {
    TestRunner.addResult('Tests accessibility in the Computed Styles pane using the axe-core linter');
    await UI.viewManager.showView('elements');
    await ElementsTestRunner.showComputedStyles();
    const panel = Elements.ElementsPanel.instance();
    const element = panel.computedStyleWidget.element;

    await AxeCoreTestRunner.runValidation(element, DEFAULT_RULESET);
  }

  TestRunner.runAsyncTestSuite(tests);
})();
