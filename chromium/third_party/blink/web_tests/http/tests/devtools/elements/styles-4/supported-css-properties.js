// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import {TestRunner} from 'test_runner';

(async function() {
  TestRunner.addResult(`Test supported CSS properties.\n`);
  await TestRunner.showPanel('elements');

  var marginLonghands = SDK.cssMetadata().getLonghands('margin');
  marginLonghands.sort();
  TestRunner.addResult('Margin longhands: ' + marginLonghands.join(', '));
  TestRunner.completeTest();
})();
