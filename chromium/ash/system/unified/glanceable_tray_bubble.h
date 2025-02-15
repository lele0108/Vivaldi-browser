// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_SYSTEM_UNIFIED_GLANCEABLE_TRAY_BUBBLE_H_
#define ASH_SYSTEM_UNIFIED_GLANCEABLE_TRAY_BUBBLE_H_

#include "ash/system/tray/tray_bubble_base.h"
#include "ash/system/unified/date_tray.h"
#include "base/memory/raw_ptr.h"

namespace ash {

class GlanceableTrayBubbleView;
class TasksBubbleView;

// Manages the bubble that contains GlanceableTrayView.
// Shows the bubble on the constructor, and closes the bubble on the destructor.
class ASH_EXPORT GlanceableTrayBubble : public TrayBubbleBase {
 public:
  explicit GlanceableTrayBubble(DateTray* tray);

  GlanceableTrayBubble(const GlanceableTrayBubble&) = delete;
  GlanceableTrayBubble& operator=(const GlanceableTrayBubble&) = delete;

  ~GlanceableTrayBubble() override;

  // views::WidgetObserver:
  void OnWidgetDestroying(views::Widget* widget) override;

  // TrayBubbleBase:
  TrayBackgroundView* GetTray() const override;
  TrayBubbleView* GetBubbleView() const override;
  views::Widget* GetBubbleWidget() const override;
  bool IsBubbleActive() const;

  TasksBubbleView* GetTasksView() const;

 private:
  void UpdateBubble();

  // Owner of this class.
  raw_ptr<DateTray, ExperimentalAsh> tray_;

  // Widget that contains GlanceableTrayView. Unowned.
  // When the widget is closed by deactivation, |bubble_widget_| pointer is
  // invalidated and we have to delete GlanceableTrayBubble by calling
  // DateTray::CloseBubble().
  // In order to do this, we observe OnWidgetDestroying().
  raw_ptr<views::Widget, ExperimentalAsh> bubble_widget_ = nullptr;

  // Main bubble view anchored to `tray_`.
  raw_ptr<GlanceableTrayBubbleView, ExperimentalAsh> bubble_view_ = nullptr;
};

}  // namespace ash

#endif  // ASH_SYSTEM_UNIFIED_GLANCEABLE_TRAY_BUBBLE_H_
