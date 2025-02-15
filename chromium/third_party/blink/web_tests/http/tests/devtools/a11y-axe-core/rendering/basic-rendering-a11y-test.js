// Copyright 2019 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';
import {AxeCoreTestRunner} from 'axe_core_test_runner';

(async function() {

  TestRunner.addResult('Tests accessibility in the rendering view using the axe-core linter.');
  await UI.viewManager.showView('rendering');
  const renderingView = await UI.viewManager.view('rendering').widget();
  await AxeCoreTestRunner.runValidation(renderingView.element);

  TestRunner.completeTest();
})();
