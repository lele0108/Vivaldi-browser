// Copyright 2016 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <sys/fcntl.h>
#include <sys/ioctl.h>

#include "base/files/file_enumerator.h"
#include "base/posix/eintr_wrapper.h"
#include "base/run_loop.h"
#include "base/task/single_thread_task_runner.h"
#include "base/test/gmock_callback_support.h"
#include "base/test/task_environment.h"
#include "build/build_config.h"
#include "media/capture/video/linux/v4l2_capture_delegate.h"
#include "media/capture/video/mock_video_capture_device_client.h"
#include "media/capture/video/video_capture_device.h"
#include "media/capture/video/video_capture_device_descriptor.h"
#include "media/capture/video_capture_types.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

using base::test::RunClosure;
using ::testing::_;

namespace media {

namespace {

// Base id and class identifiers for Controls to be modified and later tested
// agains default values.
static struct {
  uint32_t control_base;
  uint32_t class_id;
} const kControls[] = {{V4L2_CID_USER_BASE, V4L2_CID_USER_CLASS},
                       {V4L2_CID_CAMERA_CLASS_BASE, V4L2_CID_CAMERA_CLASS}};

// Determines if |control_id| is special, i.e. controls another one's state, or
// if it should be denied (see https://crbug.com/697885).
#if !defined(V4L2_CID_PAN_SPEED)
#define V4L2_CID_PAN_SPEED (V4L2_CID_CAMERA_CLASS_BASE + 32)
#endif
#if !defined(V4L2_CID_TILT_SPEED)
#define V4L2_CID_TILT_SPEED (V4L2_CID_CAMERA_CLASS_BASE + 33)
#endif
#if !defined(V4L2_CID_PANTILT_CMD)
#define V4L2_CID_PANTILT_CMD (V4L2_CID_CAMERA_CLASS_BASE + 34)
#endif
static bool IsSpecialOrBlockedControl(int control_id) {
  switch (control_id) {
    case V4L2_CID_AUTO_WHITE_BALANCE:
    case V4L2_CID_EXPOSURE_AUTO:
    case V4L2_CID_EXPOSURE_AUTO_PRIORITY:
    case V4L2_CID_FOCUS_AUTO:
    case V4L2_CID_PAN_RELATIVE:
    case V4L2_CID_TILT_RELATIVE:
    case V4L2_CID_PAN_RESET:
    case V4L2_CID_TILT_RESET:
    case V4L2_CID_PAN_ABSOLUTE:
    case V4L2_CID_TILT_ABSOLUTE:
    case V4L2_CID_ZOOM_ABSOLUTE:
    case V4L2_CID_ZOOM_RELATIVE:
    case V4L2_CID_ZOOM_CONTINUOUS:
    case V4L2_CID_PAN_SPEED:
    case V4L2_CID_TILT_SPEED:
    case V4L2_CID_PANTILT_CMD:
      return true;
  }
  return false;
}

static void SetControlsToMaxValues(int device_fd) {
  // Set V4L2_CID_AUTO_WHITE_BALANCE to false first.
  v4l2_control auto_white_balance = {};
  auto_white_balance.id = V4L2_CID_AUTO_WHITE_BALANCE;
  auto_white_balance.value = false;
  if (HANDLE_EINTR(ioctl(device_fd, VIDIOC_S_CTRL, &auto_white_balance)) < 0)
    DPLOG(ERROR) << "VIDIOC_S_CTRL";

  std::vector<struct v4l2_ext_control> special_camera_controls;
  // Set V4L2_CID_EXPOSURE_AUTO to V4L2_EXPOSURE_MANUAL.
  v4l2_ext_control auto_exposure = {};
  auto_exposure.id = V4L2_CID_EXPOSURE_AUTO;
  auto_exposure.value = V4L2_EXPOSURE_MANUAL;
  special_camera_controls.push_back(auto_exposure);
  // Set V4L2_CID_EXPOSURE_AUTO_PRIORITY to false.
  v4l2_ext_control priority_auto_exposure = {};
  priority_auto_exposure.id = V4L2_CID_EXPOSURE_AUTO_PRIORITY;
  priority_auto_exposure.value = false;
  special_camera_controls.push_back(priority_auto_exposure);
  // Set V4L2_CID_FOCUS_AUTO to false.
  v4l2_ext_control auto_focus = {};
  auto_focus.id = V4L2_CID_FOCUS_AUTO;
  auto_focus.value = false;
  special_camera_controls.push_back(auto_focus);

  struct v4l2_ext_controls camera_ext_controls = {};
  camera_ext_controls.ctrl_class = V4L2_CID_CAMERA_CLASS;
  camera_ext_controls.count = special_camera_controls.size();
  camera_ext_controls.controls = special_camera_controls.data();
  if (HANDLE_EINTR(ioctl(device_fd, VIDIOC_S_EXT_CTRLS, &camera_ext_controls)) <
      0) {
    DPLOG(ERROR) << "VIDIOC_S_EXT_CTRLS";
  }

  for (const auto& control : kControls) {
    std::vector<struct v4l2_ext_control> camera_controls;

    v4l2_queryctrl range = {};
    range.id = control.control_base | V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == HANDLE_EINTR(ioctl(device_fd, VIDIOC_QUERYCTRL, &range))) {
      if (V4L2_CTRL_ID2CLASS(range.id) != V4L2_CTRL_ID2CLASS(control.class_id))
        break;
      range.id |= V4L2_CTRL_FLAG_NEXT_CTRL;

      if (IsSpecialOrBlockedControl(range.id & ~V4L2_CTRL_FLAG_NEXT_CTRL))
        continue;
      DVLOG(1) << __func__ << " " << range.name << " set to " << range.maximum;

      struct v4l2_ext_control ext_control = {};
      ext_control.id = range.id & ~V4L2_CTRL_FLAG_NEXT_CTRL;
      ext_control.value = range.maximum;
      camera_controls.push_back(ext_control);
    }

    if (!camera_controls.empty()) {
      struct v4l2_ext_controls ext_controls = {};
      ext_controls.ctrl_class = control.class_id;
      ext_controls.count = camera_controls.size();
      ext_controls.controls = camera_controls.data();
      if (HANDLE_EINTR(ioctl(device_fd, VIDIOC_S_EXT_CTRLS, &ext_controls)) < 0)
        DPLOG(ERROR) << "VIDIOC_S_EXT_CTRLS";
    }

    range.id = control.control_base | V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == HANDLE_EINTR(ioctl(device_fd, VIDIOC_QUERYCTRL, &range))) {
      if (V4L2_CTRL_ID2CLASS(range.id) != V4L2_CTRL_ID2CLASS(control.class_id))
        break;
      range.id |= V4L2_CTRL_FLAG_NEXT_CTRL;

      if (IsSpecialOrBlockedControl(range.id & ~V4L2_CTRL_FLAG_NEXT_CTRL))
        continue;
      DVLOG(1) << __func__ << " " << range.name << " set to " << range.maximum;

      v4l2_control readback = {};
      readback.id = range.id & ~V4L2_CTRL_FLAG_NEXT_CTRL;
      if (HANDLE_EINTR(ioctl(device_fd, VIDIOC_G_CTRL, &readback)) < 0)
        DPLOG(ERROR) << range.name << ", failed to be read.";
      EXPECT_EQ(range.maximum, readback.value) << " control " << range.name
                                               << " didnt set correctly";
    }
  }
}

static void VerifyUserControlsAreSetToDefaultValues(int device_fd) {
  auto do_ioctl = base::BindRepeating(
      [](int device_fd, int request, void* argp) {
        return HANDLE_EINTR(ioctl(device_fd, request, argp));
      },
      device_fd);

  for (const auto& control : kControls) {
    v4l2_queryctrl range = {};
    // Start right below the base so that the first next retrieved control ID
    // is always the first available control ID within the class even if that
    // control ID is equal to the base (V4L2_CID_BRIGHTNESS equals to
    // V4L2_CID_USER_BASE).
    range.id = (control.control_base - 1) | V4L2_CTRL_FLAG_NEXT_CTRL;
    while (0 == HANDLE_EINTR(ioctl(device_fd, VIDIOC_QUERYCTRL, &range))) {
      if (V4L2_CTRL_ID2CLASS(range.id) != V4L2_CTRL_ID2CLASS(control.class_id))
        break;

      DVLOG(1) << __func__ << " " << range.name << ": " << range.minimum << "-"
               << range.maximum << ", default: " << range.default_value;

      v4l2_control current = {};
      current.id = range.id;

      // Prepare to query for the next control as `range` is an in-out
      // parameter.
      range.id |= V4L2_CTRL_FLAG_NEXT_CTRL;

      if (range.flags & (V4L2_CTRL_FLAG_DISABLED | V4L2_CTRL_FLAG_READ_ONLY)) {
        // Permanently disabled or permanently read-only.
        continue;
      }
      if (V4L2CaptureDelegate::IsBlockedControl(current.id) ||
          !V4L2CaptureDelegate::IsControllableControl(current.id, do_ioctl)) {
        // Skip controls which are blocked and controls which are controlled
        // by special controls which are in automatic states.
        continue;
      }

      if (HANDLE_EINTR(ioctl(device_fd, VIDIOC_G_CTRL, &current)) < 0)
        DPLOG(ERROR) << "control " << range.name;

      EXPECT_EQ(range.default_value, current.value)
          << " control " << range.name << " didn't reset correctly";
    }
  }
}

class V4L2CaptureDelegateTest : public ::testing::Test {
 public:
  V4L2CaptureDelegateTest()
      : device_descriptor_("Device 0", "/dev/video0"),
        v4l2_(new V4L2CaptureDeviceImpl()),
        delegate_(std::make_unique<V4L2CaptureDelegate>(
            v4l2_.get(),
            device_descriptor_,
            base::SingleThreadTaskRunner::GetCurrentDefault(),
            50,
            0)) {}
  ~V4L2CaptureDelegateTest() override = default;

  base::test::SingleThreadTaskEnvironment task_environment_;
  VideoCaptureDeviceDescriptor device_descriptor_;
  scoped_refptr<V4L2CaptureDevice> v4l2_;
  std::unique_ptr<V4L2CaptureDelegate> delegate_;
};

}  // anonymous namespace

// Fails on Linux, see crbug/732355
#if BUILDFLAG(IS_LINUX)
#define MAYBE_CreateAndDestroyAndVerifyControls \
  DISABLED_CreateAndDestroyAndVerifyControls
#else
#define MAYBE_CreateAndDestroyAndVerifyControls \
  CreateAndDestroyAndVerifyControls
#endif
TEST_F(V4L2CaptureDelegateTest, MAYBE_CreateAndDestroyAndVerifyControls) {
  // Check that there is at least a video device, otherwise bail.
  const base::FilePath path("/dev/");
  base::FileEnumerator enumerator(path, false, base::FileEnumerator::FILES,
                                  "video*");
  if (enumerator.Next().empty()) {
    DLOG(INFO) << " No devices found, skipping test";
    return;
  }

  // Open device, manipulate user and camera controls, and close it.
  {
    base::ScopedFD device_fd(
        HANDLE_EINTR(open(device_descriptor_.device_id.c_str(), O_RDWR)));
    ASSERT_TRUE(device_fd.is_valid());

    SetControlsToMaxValues(device_fd.get());

    base::RunLoop().RunUntilIdle();
  }

  // Start and stop capturing, triggering the resetting of user and camera
  // control values.
  {
    std::unique_ptr<MockVideoCaptureDeviceClient> client(
        new MockVideoCaptureDeviceClient());
    MockVideoCaptureDeviceClient* client_ptr = client.get();
    EXPECT_CALL(*client_ptr, OnStarted());
    delegate_->AllocateAndStart(320 /* width */, 240 /* height */,
                                10.0 /* frame_rate */, std::move(client));

    base::RunLoop run_loop;
    base::RepeatingClosure quit_closure = run_loop.QuitClosure();
    EXPECT_CALL(*client_ptr, OnIncomingCapturedData(_, _, _, _, _, _, _, _, _))
        .Times(1)
        .WillOnce(RunClosure(quit_closure));
    run_loop.Run();

    delegate_->StopAndDeAllocate();
    base::RunLoop().RunUntilIdle();
  }

  // Reopen the device and verify all user and camera controls should be back to
  // their |default_value|s.
  {
    base::ScopedFD device_fd(
        HANDLE_EINTR(open(device_descriptor_.device_id.c_str(), O_RDWR)));
    ASSERT_TRUE(device_fd.is_valid());
    VerifyUserControlsAreSetToDefaultValues(device_fd.get());
  }
}

}  // namespace media
