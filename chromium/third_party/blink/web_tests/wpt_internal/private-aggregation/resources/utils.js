// Payload with contributions [{bucket: 1n, value: 2}]
const ONE_CONTRIBUTION_EXAMPLE_PAYLOAD =
    'omRkYXRhgaJldmFsdWVEAAAAAmZidWNrZXRQAAAAAAAAAAAAAAAAAAAAAWlvcGVyYXRpb25paGlzdG9ncmFt';

// Payload with contributions [{bucket: 1n, value: 2}, {bucket: 3n, value: 4}]
const MULTIPLE_CONTRIBUTIONS_EXAMPLE_PAYLOAD =
    'omRkYXRhgqJldmFsdWVEAAAAAmZidWNrZXRQAAAAAAAAAAAAAAAAAAAAAaJldmFsdWVEAAAABGZidWNrZXRQAAAAAAAAAAAAAAAAAAAAA2lvcGVyYXRpb25paGlzdG9ncmFt';

/**
 * Delay method that waits for prescribed number of milliseconds.
 */
const delay = ms => new Promise(resolve => step_timeout(resolve, ms));

/**
 * Polls the given `url` to retrieve reports sent there. Once the reports are
 * received, returns the list of reports. Returns null if the timeout is reached
 * before a report is available.
 */
const pollReports = async (url, origin = location.origin, timeout = 1000 /*ms*/) => {
  let startTime = performance.now();
  while (performance.now() - startTime < timeout) {
    const resp = await fetch(new URL(url, origin));
    const payload = await resp.json();
    if (payload.length > 0) {
      return payload;
    }
    await delay(/*ms=*/ 100);
  }
  return null;
};

/**
 * Verifies that a report's shared_info string is serialized JSON with the
 * expected fields. `is_debug_enabled` should be a boolean corresponding to
 * whether debug mode is expected to be enabled for this report.
 */
const verifySharedInfo = (shared_info_str, is_debug_enabled) => {
  shared_info = JSON.parse(shared_info_str);
  assert_equals(shared_info.api, 'shared-storage');
  if (is_debug_enabled) {
    assert_equals(shared_info.debug_mode, 'enabled');
  } else {
    assert_not_own_property(shared_info, 'debug_mode');
  }

  const uuid_regex = RegExp(
      '^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$');
  assert_own_property(shared_info, 'report_id');
  assert_true(uuid_regex.test(shared_info.report_id));

  assert_equals(shared_info.reporting_origin, location.origin);

  // The amount of delay is implementation-defined.
  const integer_regex = RegExp('^[0-9]*$');
  assert_own_property(shared_info, 'scheduled_report_time');
  assert_true(integer_regex.test(shared_info.scheduled_report_time));

  assert_equals(shared_info.version, '0.1');

  // Check there are no extra keys
  assert_equals(Object.keys(shared_info).length, is_debug_enabled ? 6 : 5);
};

/**
 * Verifies that an report's aggregation_service_payloads has the expected
 * fields. The `expected_cleartext_payload` should be the expected value of
 * debug_cleartext_payload or undefined if debug mode is disabled.
 */
const verifyAggregationServicePayloads = (aggregation_service_payloads, expected_cleartext_payload) => {
  assert_equals(aggregation_service_payloads.length, 1);
  const payload_obj = aggregation_service_payloads[0];

  assert_own_property(payload_obj, 'key_id');
  // The only id specified in the test key file.
  assert_equals(payload_obj.key_id, 'example_id');

  assert_own_property(payload_obj, 'payload');
  // Check the payload is base64 encoded. We do not decrypt the payload to test
  // its contents.
  atob(payload_obj.payload);

  if (expected_cleartext_payload) {
    assert_own_property(payload_obj, 'debug_cleartext_payload');
    assert_equals(payload_obj.debug_cleartext_payload, expected_cleartext_payload);
  }

  // Check there are no extra keys
  assert_equals(Object.keys(payload_obj).length, expected_cleartext_payload ? 3 : 2);
};

/**
 * Verifies that an report has the expected fields. `is_debug_enabled` should be
 * a boolean corresponding to whether debug mode is expected to be enabled for
 * this report. `debug_key` should be the debug key if set; otherwise,
 * undefined. The `expected_cleartext_payload` should be the expected value of
 * debug_cleartext_payload if debug mode is enabled; otherwise, undefined.
 */
const verifyReport = (report, is_debug_enabled, debug_key, expected_cleartext_payload, context_id = undefined) => {
  if (debug_key || expected_cleartext_payload) {
    // A debug key cannot be set without debug mode being enabled and the
    // `expected_cleartext_payload` should be undefined if debug mode is not
    // enabled.
    assert_true(is_debug_enabled);
  }

  assert_own_property(report, 'shared_info');
  verifySharedInfo(report.shared_info, is_debug_enabled);

  if (debug_key) {
    assert_own_property(report, 'debug_key');
    assert_equals(report.debug_key, debug_key);
  } else {
    assert_not_own_property(report, 'debug_key');
  }

  assert_own_property(report, 'aggregation_service_payloads');
  verifyAggregationServicePayloads(report.aggregation_service_payloads, expected_cleartext_payload);

  if (context_id) {
    assert_own_property(report, 'context_id');
    assert_equals(report.context_id, context_id);
  } else {
    assert_not_own_property(report, 'context_id');
  }

  // Check there are no extra keys
  let expected_length = 2;
  if (debug_key) {
    ++expected_length;
  }
  if (context_id) {
    ++expected_length;
  }
  assert_equals(Object.keys(report).length, expected_length);
};

/**
 * Verifies that two reports are identical except for the payload (which is
 * encrypted and thus non-deterministic). Assumes that reports are well formed,
 * so should only be called after verifyReport().
 */
const verifyReportsIdenticalExceptPayload = (report_a, report_b) => {
  report_a.aggregation_service_payloads[0].payload = "PAYLOAD";
  report_b.aggregation_service_payloads[0].payload = "PAYLOAD";

  assert_equals(JSON.stringify(report_a), JSON.stringify(report_b));
}