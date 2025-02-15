// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "cc/metrics/scroll_jank_dropped_frame_tracker.h"

#include <algorithm>

#include "base/metrics/histogram_functions.h"
#include "base/metrics/histogram_macros.h"
#include "base/strings/strcat.h"
#include "base/trace_event/common/trace_event_common.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_id_helper.h"
#include "base/trace_event/typed_macros.h"

namespace cc {

namespace {
enum class PerScrollHistogramType {
  // For Event.ScrollJank.DelayedFramesPercentage.PerScroll.* histograms.
  kPercentage = 0,
  // For Event.ScrollJank.MissedVsyncsMax.PerScroll.* histograms.
  kMax = 1,
  // For Event.ScrollJank.MissedVsyncsSum.PerScroll.* histograms.
  kSum = 2,
};

// Histogram min, max and no. of buckets.
constexpr int kVsyncCountsMin = 1;
constexpr int kVsyncCountsMax = 50;
constexpr int kVsyncCountsBuckets = 25;

const char* GetPerScrollHistogramName(int num_frames,
                                      PerScrollHistogramType type) {
  DCHECK_GT(num_frames, 0);
  if (type == PerScrollHistogramType::kPercentage) {
    if (num_frames <= 16) {
      return "Event.ScrollJank.DelayedFramesPercentage.PerScroll.Small";
    } else if (num_frames <= 64) {
      return "Event.ScrollJank.DelayedFramesPercentage.PerScroll.Medium";
    } else {
      return "Event.ScrollJank.DelayedFramesPercentage.PerScroll.Large";
    }
  } else if (type == PerScrollHistogramType::kMax) {
    if (num_frames <= 16) {
      return "Event.ScrollJank.MissedVsyncsMax.PerScroll.Small";
    } else if (num_frames <= 64) {
      return "Event.ScrollJank.MissedVsyncsMax.PerScroll.Medium";
    } else {
      return "Event.ScrollJank.MissedVsyncsMax.PerScroll.Large";
    }
  } else {
    DCHECK_EQ(type, PerScrollHistogramType::kSum);
    if (num_frames <= 16) {
      return "Event.ScrollJank.MissedVsyncsSum.PerScroll.Small";
    } else if (num_frames <= 64) {
      return "Event.ScrollJank.MissedVsyncsSum.PerScroll.Medium";
    } else {
      return "Event.ScrollJank.MissedVsyncsSum.PerScroll.Large";
    }
  }
}
}  // namespace

ScrollJankDroppedFrameTracker::ScrollJankDroppedFrameTracker() {
  // Not initializing with 0 because the first frame in first window will be
  // always deemed non-janky which makes the metric slightly biased. Setting
  // it to -1 essentially ignores first frame.
  fixed_window_.num_presented_frames = -1;
}

ScrollJankDroppedFrameTracker::~ScrollJankDroppedFrameTracker() {
  if (per_scroll_.has_value()) {
    // Per scroll metrics for a given scroll are emitted at the start of next
    // scroll. Emittimg from here makes sure we don't loose the data for last
    // scroll.
    EmitPerScrollHistogramsAndResetCounters();
  }
}

void ScrollJankDroppedFrameTracker::EmitPerScrollHistogramsAndResetCounters() {
  // There should be at least one presented frame given the method is only
  // called after we have a successful presentation.
  if (per_scroll_->num_presented_frames == 0) {
    // TODO(1464878): Debug cases where we can have 0 presented frames.
    TRACE_EVENT_INSTANT("input", "NoPresentedFramesInScroll");
    return;
  }
  // Emit non-bucketed histograms.
  int delayed_frames_percentage =
      (100 * per_scroll_->missed_frames) / per_scroll_->num_presented_frames;
  UMA_HISTOGRAM_PERCENTAGE(kDelayedFramesPerScrollHistogram,
                           delayed_frames_percentage);
  UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsMaxPerScrollHistogram,
                              per_scroll_->max_missed_vsyncs, kVsyncCountsMin,
                              kVsyncCountsMax, kVsyncCountsBuckets);
  UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsSumPerScrollHistogram,
                              per_scroll_->missed_vsyncs, kVsyncCountsMin,
                              kVsyncCountsMax, kVsyncCountsBuckets);
  // Emit bucketed histogram.
  base::UmaHistogramPercentage(
      GetPerScrollHistogramName(per_scroll_->num_presented_frames,
                                PerScrollHistogramType::kPercentage),
      delayed_frames_percentage);
  base::UmaHistogramCustomCounts(
      GetPerScrollHistogramName(per_scroll_->num_presented_frames,
                                PerScrollHistogramType::kMax),
      per_scroll_->max_missed_vsyncs, kVsyncCountsMin, kVsyncCountsMax,
      kVsyncCountsBuckets);
  base::UmaHistogramCustomCounts(
      GetPerScrollHistogramName(per_scroll_->num_presented_frames,
                                PerScrollHistogramType::kSum),
      per_scroll_->missed_vsyncs, kVsyncCountsMin, kVsyncCountsMax,
      kVsyncCountsBuckets);

  per_scroll_->missed_frames = 0;
  per_scroll_->missed_vsyncs = 0;
  per_scroll_->num_presented_frames = 0;
  per_scroll_->max_missed_vsyncs = 0;
}

void ScrollJankDroppedFrameTracker::EmitPerWindowHistogramsAndResetCounters() {
  DCHECK_EQ(fixed_window_.num_presented_frames, kHistogramEmitFrequency);

  UMA_HISTOGRAM_PERCENTAGE(
      kDelayedFramesWindowHistogram,
      (100 * fixed_window_.missed_frames) / kHistogramEmitFrequency);
  UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsSumInWindowHistogram,
                              fixed_window_.missed_vsyncs, kVsyncCountsMin,
                              kVsyncCountsMax, kVsyncCountsBuckets);
  UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsMaxInWindowHistogram,
                              fixed_window_.max_missed_vsyncs, kVsyncCountsMin,
                              kVsyncCountsMax, kVsyncCountsBuckets);

  fixed_window_.missed_frames = 0;
  fixed_window_.missed_vsyncs = 0;
  fixed_window_.max_missed_vsyncs = 0;
  // We don't need to reset it to -1 because after the first window we always
  // have a valid previous frame data to compare the first frame of window.
  fixed_window_.num_presented_frames = 0;
}

void ScrollJankDroppedFrameTracker::ReportLatestPresentationData(
    base::TimeTicks first_input_generation_ts,
    base::TimeTicks last_input_generation_ts,
    base::TimeTicks presentation_ts,
    base::TimeDelta vsync_interval) {
  if ((last_input_generation_ts < first_input_generation_ts) ||
      (presentation_ts <= last_input_generation_ts)) {
    // TODO(crbug/1447358): Investigate when these edge cases can be triggered
    // in field and web tests. We have already seen this triggered in field, and
    // some web tests where an event with null(0) timestamp gets coalesced with
    // a "normal" input.
    return;
  }
  // TODO(b/276722271) : Analyze and reduce these cases of out of order
  // frame termination.
  if (presentation_ts <= prev_presentation_ts_) {
    TRACE_EVENT_INSTANT("input", "OutOfOrderTerminatedFrame");
    return;
  }

  // `per_scroll_` is initialized in OnScrollStarted when we see
  // FIRST_GESTURE_SCROLL_UPDATE event. But in some rare scenarios we don't see
  // the FIRST_GESTURE_SCROLL_UPDATE events on scroll start.
  if (!per_scroll_.has_value()) {
    per_scroll_ = JankData();
  }

  // The presentation delta is usually 16.6ms for 60 Hz devices,
  // but sometimes random errors result in a delta of up to 20ms
  // as observed in traces. This adds an error margin of 1/2 a
  // vsync before considering the Vsync missed.
  bool missed_frame = (presentation_ts - prev_presentation_ts_) >
                      (vsync_interval + vsync_interval / 2);
  bool input_available =
      (first_input_generation_ts - prev_last_input_generation_ts_) <
      (vsync_interval + vsync_interval / 2);
  if (missed_frame && input_available) {
    ++fixed_window_.missed_frames;
    ++per_scroll_->missed_frames;
    int curr_frame_missed_vsyncs =
        (presentation_ts - prev_presentation_ts_ - (vsync_interval / 2)) /
        vsync_interval;
    UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsPerFrameHistogram,
                                curr_frame_missed_vsyncs, kVsyncCountsMin,
                                kVsyncCountsMax, kVsyncCountsBuckets);
    fixed_window_.missed_vsyncs += curr_frame_missed_vsyncs;
    per_scroll_->missed_vsyncs += curr_frame_missed_vsyncs;
    if (curr_frame_missed_vsyncs > per_scroll_->max_missed_vsyncs) {
      per_scroll_->max_missed_vsyncs = curr_frame_missed_vsyncs;
    }
    if (curr_frame_missed_vsyncs > fixed_window_.max_missed_vsyncs) {
      fixed_window_.max_missed_vsyncs = curr_frame_missed_vsyncs;
    }

    TRACE_EVENT_INSTANT(
        "input,input.scrolling", "MissedFrame", "per_scroll_->missed_frames",
        per_scroll_->missed_frames, "per_scroll_->missed_vsyncs",
        per_scroll_->missed_vsyncs, "vsync_interval", vsync_interval);
  } else {
    UMA_HISTOGRAM_CUSTOM_COUNTS(kMissedVsyncsPerFrameHistogram, 0,
                                kVsyncCountsMin, kVsyncCountsMax,
                                kVsyncCountsBuckets);
  }

  ++fixed_window_.num_presented_frames;
  ++per_scroll_->num_presented_frames;
  if (fixed_window_.num_presented_frames == kHistogramEmitFrequency) {
    EmitPerWindowHistogramsAndResetCounters();
  }
  DCHECK_LT(fixed_window_.num_presented_frames, kHistogramEmitFrequency);

  prev_presentation_ts_ = presentation_ts;
  prev_last_input_generation_ts_ = last_input_generation_ts;
}

void ScrollJankDroppedFrameTracker::OnScrollStarted() {
  if (per_scroll_.has_value()) {
    EmitPerScrollHistogramsAndResetCounters();
  } else {
    per_scroll_ = JankData();
  }
}

}  // namespace cc
