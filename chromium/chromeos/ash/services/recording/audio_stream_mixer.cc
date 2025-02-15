// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chromeos/ash/services/recording/audio_stream_mixer.h"

#include <algorithm>
#include <memory>

#include "base/functional/bind.h"
#include "base/task/bind_post_task.h"
#include "chromeos/ash/services/recording/audio_capture_util.h"
#include "chromeos/ash/services/recording/audio_capturer.h"
#include "chromeos/ash/services/recording/audio_stream.h"
#include "chromeos/ash/services/recording/recording_service_constants.h"
#include "media/base/audio_bus.h"
#include "media/base/audio_parameters.h"

namespace recording {

namespace {

// When the number of audio frames in a stream exceeds the maximum value (which
// is the number of frames needed to exceed the `kMaxAudioStreamFifoDuration` at
// the default sample rate), this function returns true.
bool DidStreamReachMaxDuration(const AudioStream& stream) {
  static const int kMaxFrames =
      audio_capture_util::NumberOfAudioFramesInDuration(
          audio_capture_util::kMaxAudioStreamFifoDuration);
  return stream.total_frames() >= kMaxFrames;
}

}  // namespace

AudioStreamMixer::AudioStreamMixer(PassKey) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

AudioStreamMixer::AudioStreamMixer(PassKey, OnAudioMixerOutputCallback callback)
    : on_mixer_output_callback_(std::move(callback)) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

AudioStreamMixer::~AudioStreamMixer() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
}

// static
base::SequenceBound<AudioStreamMixer> AudioStreamMixer::Create(
    scoped_refptr<base::SequencedTaskRunner> task_runner) {
  return base::SequenceBound<AudioStreamMixer>(std::move(task_runner),
                                               PassKey());
}

void AudioStreamMixer::AddAudioCapturer(
    base::StringPiece device_id,
    mojo::PendingRemote<media::mojom::AudioStreamFactory> audio_stream_factory,
    bool use_automatic_gain_control,
    bool use_echo_canceller) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  auto audio_params = audio_capture_util::GetAudioCaptureParameters();
  int effects = media::AudioParameters::NO_EFFECTS;
  if (use_automatic_gain_control) {
    effects |= media::AudioParameters::AUTOMATIC_GAIN_CONTROL;
  }
  if (use_echo_canceller) {
    effects |= media::AudioParameters::ECHO_CANCELLER;
  }
  audio_params.set_effects(effects);

  streams_.emplace_back(std::make_unique<AudioStream>(device_id));

  // TODO(b/286325436): Refactor this to make sure audio mixing is not done on
  // the main thread of the recording service.
  audio_capturers_.emplace_back(std::make_unique<AudioCapturer>(
      device_id, std::move(audio_stream_factory), audio_params,
      base::BindPostTaskToCurrentDefault(base::BindRepeating(
          &AudioStreamMixer::OnAudioCaptured, weak_ptr_factory_.GetWeakPtr(),
          streams_.back().get()))));
}

void AudioStreamMixer::Start(OnAudioMixerOutputCallback callback) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  on_mixer_output_callback_ = std::move(callback);
  DCHECK(on_mixer_output_callback_);
  for (auto& capturer : audio_capturers_) {
    capturer->Start();
  }
}

void AudioStreamMixer::Stop() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  for (auto& capturer : audio_capturers_) {
    capturer->Stop();
  }
}

// static
AudioStreamMixer::PassKey AudioStreamMixer::PassKeyForTesting() {
  return PassKey();
}

int AudioStreamMixer::GetNumberOfCapturers() const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);
  return audio_capturers_.size();
}

void AudioStreamMixer::OnAudioCaptured(
    AudioStream* audio_stream,
    std::unique_ptr<media::AudioBus> audio_bus,
    base::TimeTicks audio_capture_time) {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  // Note that the very first received audio bus doesn't necessarily have the
  // smallest timestamp among all the streams managed by this mixer.
  audio_stream->AppendAudioBus(std::move(audio_bus), audio_capture_time);

  MaybeMixAndOutput();
}

void AudioStreamMixer::MaybeMixAndOutput() {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::TimeTicks bus_timestamp;
  auto mixer_bus = CreateMixerBus(bus_timestamp);
  if (!mixer_bus) {
    return;
  }

  CHECK(!bus_timestamp.is_null());
  for (auto& stream : streams_) {
    if (stream->empty()) {
      // Empty streams have nothing to mix with other streams.
      continue;
    }

    CHECK_GE(stream->begin_timestamp(), bus_timestamp);
    const auto gap_frames = audio_capture_util::NumberOfAudioFramesInDuration(
        stream->begin_timestamp() - bus_timestamp);
    const auto frames_to_consume = mixer_bus->frames() - gap_frames;
    if (frames_to_consume <= 0) {
      // This happens when there is no overlap between this stream and the mixer
      // output bus. For example:
      //
      //                                      +-------+
      // Stream 1:                            |       |
      //                                      +-------+
      //                    +-------+-------+
      // Stream 2:          |       |       |
      //                    +-------+-------+
      //
      //                    +---------------+
      // Mixer bus:         |               |
      //                    +---------------+
      //
      // In this case, there's nothing to mix from Stream 1 in the above
      // diagram.
      continue;
    }

    stream->ConsumeAndAccumulateTo(/*destination=*/mixer_bus.get(),
                                   /*destination_start_frame=*/gap_frames,
                                   frames_to_consume);
  }

  on_mixer_output_callback_.Run(std::move(mixer_bus), bus_timestamp);
}

std::unique_ptr<media::AudioBus> AudioStreamMixer::CreateMixerBus(
    base::TimeTicks& out_bus_timestamp) const {
  DCHECK_CALLED_ON_VALID_SEQUENCE(sequence_checker_);

  base::TimeTicks min_end_timestamp = base::TimeTicks::Max();
  base::TimeTicks min_begin_timestamp = base::TimeTicks::Max();
  out_bus_timestamp = base::TimeTicks();

  // If any of the streams is almost full, we must consume regardless of the
  // situation, even if some of the streams are empty.
  bool any_stream_reached_max_duration = false;
  // If any of the streams didn't receive any frames yet.
  bool any_stream_empty = false;

  for (const auto& stream : streams_) {
    any_stream_reached_max_duration |= DidStreamReachMaxDuration(*stream);
    const bool is_stream_empty = stream->empty();
    any_stream_empty |= is_stream_empty;

    // Do not consider empty streams in the overlap range calculation, since an
    // empty stream means that either it had never received any frames ever yet,
    // or it had, but they all had been consumed previously.
    if (!is_stream_empty && stream->begin_timestamp() < min_begin_timestamp) {
      min_begin_timestamp = stream->begin_timestamp();
    }

    if (!is_stream_empty && stream->end_timestamp() < min_end_timestamp) {
      min_end_timestamp = stream->end_timestamp();
    }
  }

  if (any_stream_empty && !any_stream_reached_max_duration) {
    return nullptr;
  }

  out_bus_timestamp = min_begin_timestamp;
  CHECK_GT(min_end_timestamp, out_bus_timestamp);
  return audio_capture_util::CreateStereoZeroInitializedAudioBusForDuration(
      min_end_timestamp - out_bus_timestamp);
}

}  // namespace recording
