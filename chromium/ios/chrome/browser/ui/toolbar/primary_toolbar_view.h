// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_TOOLBAR_PRIMARY_TOOLBAR_VIEW_H_
#define IOS_CHROME_BROWSER_UI_TOOLBAR_PRIMARY_TOOLBAR_VIEW_H_

#import <UIKit/UIKit.h>

#import "ios/chrome/browser/ui/toolbar/adaptive_toolbar_view.h"

// Vivaldi
#import "ios/ui/ad_tracker_blocker/vivaldi_atb_setting_type.h"
// End Vivaldi

@class ToolbarButtonFactory;

// View for the primary toolbar. In an adaptive toolbar paradigm, this is the
// toolbar always displayed.
@interface PrimaryToolbarView : UIView<AdaptiveToolbarView>

// Initialize this View with the button `factory`. To finish the initialization
// of the view, a call to `setUp` is needed.
- (instancetype)initWithButtonFactory:(ToolbarButtonFactory*)factory
    NS_DESIGNATED_INITIALIZER;

- (instancetype)initWithFrame:(CGRect)frame NS_UNAVAILABLE;
- (instancetype)initWithCoder:(NSCoder*)aDecoder NS_UNAVAILABLE;
- (instancetype)init NS_UNAVAILABLE;

// A tappable view overlapping `locationBarContainer` used when the omnibox is
// hidden by the NTP.
@property(nonatomic, strong) UIView* fakeOmniboxTarget;

// StackView containing the leading buttons (relative to the location bar).
// It should only contain ToolbarButtons.
@property(nonatomic, strong, readonly) UIStackView* leadingStackView;
// StackView containing the trailing buttons (relative to the location bar).
// It should only contain ToolbarButtons.
@property(nonatomic, strong, readonly) UIStackView* trailingStackView;

// Button to cancel the edit of the location bar.
@property(nonatomic, strong, readonly) UIButton* cancelButton;

// Constraints to be activated when the location bar is expanded and positioned
// relatively to the cancel button.
@property(nonatomic, strong, readonly)
    NSMutableArray<NSLayoutConstraint*>* expandedConstraints;
// Constraints to be activated when the location bar is contracted with large
// padding between the location bar and the controls.
@property(nonatomic, strong, readonly)
    NSMutableArray<NSLayoutConstraint*>* contractedConstraints;
// Constraints to be activated when the location bar is expanded without cancel
// button.
@property(nonatomic, strong, readonly)
    NSMutableArray<NSLayoutConstraint*>* contractedNoMarginConstraints;

// Constraint for the bottom of the location bar.
@property(nonatomic, strong, readwrite)
    NSLayoutConstraint* locationBarBottomConstraint;

// Whether the top-left and top-right corners of the toolbar are rounded or
// square.
@property(nonatomic, assign) BOOL topCornersRounded;

// Sets all the subviews and constraints of the view. The `topSafeAnchor` needs
// to be set before calling this.
- (void)setUp;

// Adds a view overlapping `locationBarContainer` for use when the omnibox is
// hidden by the NTP.
- (void)addFakeOmniboxTarget;

// Removes `fakeOmniboxTarget` from the view hierarchy.
- (void)removeFakeOmniboxTarget;

// Vivaldi
/// Redraws the primary toolbar buttons based on device
/// orientation.
- (void)redrawToolbarButtons;
/// Used to hide and show the toolbar buttons based on orientation and omnibox
/// state.
- (void)handleToolbarButtonVisibility:(BOOL)show;
/// Update the vivaldi more actions based on web context. This is only available
/// for iPhone landscape mode.
- (void)setVivaldiMoreActionItemsWithShareState:(BOOL)enabled;
/// Update tracker blocker shield icon based on settings.
- (void)updateVivaldiShieldState:(ATBSettingType)setting;
// End Vivaldi

@end

#endif  // IOS_CHROME_BROWSER_UI_TOOLBAR_PRIMARY_TOOLBAR_VIEW_H_
