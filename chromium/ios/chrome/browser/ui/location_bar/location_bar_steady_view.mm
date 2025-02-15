// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/location_bar/location_bar_steady_view.h"

#import "base/check.h"
#import "base/check_op.h"
#import "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/shared/ui/elements/extended_touch_target_button.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/shared/ui/util/dynamic_type_util.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/ui/omnibox/omnibox_constants.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/common/ui/util/constraints_ui_util.h"
#import "ios/chrome/common/ui/util/pointer_interaction_util.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"

// Vivaldi
#import "app/vivaldi_apptools.h"
#import "ios/chrome/browser/ui/location_bar/location_bar_constants+vivaldi.h"
#import "ios/ui/ntp/vivaldi_ntp_constants.h"

using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// Length of the trailing button side.
const CGFloat kButtonSize = 24;
// Space between the location icon and the location label.
const CGFloat kLocationImageToLabelSpacing = -2.0;
// Minimal horizontal padding between the leading edge of the location bar and
// the content of the location bar.
const CGFloat kLocationBarLeadingPadding = 8.0;
// Trailing space between the trailing button and the trailing edge of the
// location bar.
const CGFloat kShareButtonTrailingSpacing = -11;
const CGFloat kVoiceSearchButtonTrailingSpacing = -7;
// Duration of display and hide animation of the badge view, in seconds.
const CGFloat kbadgeViewAnimationDuration = 0.2;
// Location label vertical offset.
const CGFloat kLocationLabelVerticalOffset = -1;
}  // namespace

@interface LocationBarSteadyView ()

// The image view displaying the current location icon (i.e. http[s] status).
@property(nonatomic, strong) UIImageView* locationIconImageView;

// The view containing the location label, and (sometimes) the location image
// view.
@property(nonatomic, strong) UIView* locationContainerView;

// Leading constraint for locationContainerView when there is no BadgeView to
// its left.
@property(nonatomic, strong)
    NSLayoutConstraint* locationContainerViewLeadingAnchorConstraint;

// The constraint that pins the trailingButton to the trailing edge of the
// location bar.
@property(nonatomic, strong)
    NSLayoutConstraint* trailingButtonTrailingAnchorConstraint;

// The trailing spacing to be used for the trailingButton. This property is
// based on the type of trailing button in use (i.e. share or voice search).
@property(nonatomic, readonly) CGFloat trailingButtonTrailingSpacing;

// Constraints to pin the badge view to the right next to the
// `locationContainerView`.
@property(nonatomic, strong)
    NSArray<NSLayoutConstraint*>* badgeViewFullScreenEnabledConstraints;

// Constraints to pin the badge view to the left side of the LocationBar.
@property(nonatomic, strong)
    NSArray<NSLayoutConstraint*>* badgeViewFullScreenDisabledConstraints;

// Constraints to hide the location image view.
@property(nonatomic, strong)
    NSArray<NSLayoutConstraint*>* hideLocationImageConstraints;

// Constraints to show the location image view.
@property(nonatomic, strong)
    NSArray<NSLayoutConstraint*>* showLocationImageConstraints;

// Elements to surface in accessibility.
@property(nonatomic, strong) NSMutableArray* accessibleElements;

@end

#pragma mark - LocationBarSteadyViewColorScheme

@implementation LocationBarSteadyViewColorScheme

+ (instancetype)standardScheme {
  LocationBarSteadyViewColorScheme* scheme =
      [[LocationBarSteadyViewColorScheme alloc] init];

  scheme.fontColor = [UIColor colorNamed:kTextPrimaryColor];
  scheme.placeholderColor = [UIColor colorNamed:kTextfieldPlaceholderColor];
  scheme.trailingButtonColor = [UIColor colorNamed:kGrey600Color];

  return scheme;
}

@end

#pragma mark - LocationBarSteadyButton

// Buttons with a darker background in highlighted state.
@interface LocationBarSteadyButton : UIButton
@end

@implementation LocationBarSteadyButton

- (instancetype)initWithFrame:(CGRect)frame {
  self = [super initWithFrame:frame];
  if (self) {
    self.pointerInteractionEnabled = YES;
  }
  return self;
}

- (void)layoutSubviews {
  [super layoutSubviews];

  if (IsVivaldiRunning()) {
    self.layer.cornerRadius = vNTPSearchBarCornerRadius;
  } else {
  self.layer.cornerRadius = self.bounds.size.height / 2.0;
  } // End Vivaldi

}

- (void)setHighlighted:(BOOL)highlighted {
  [super setHighlighted:highlighted];

  // Note: (prio@vivaldi.com) We don't want to highlight the steady view since
  // that ends up showing a visible glitch in transition as the UI begins to
  // move at the same time.
  if (IsVivaldiRunning())
    return;

  CGFloat duration = highlighted ? 0.1 : 0.2;
  [UIView animateWithDuration:duration
                        delay:0
                      options:UIViewAnimationOptionBeginFromCurrentState
                   animations:^{
                     CGFloat alpha = highlighted ? 0.07 : 0;
                     self.backgroundColor =
                         [UIColor colorWithWhite:0 alpha:alpha];
                   }
                   completion:nil];
}

@end

#pragma mark - LocationBarSteadyView

@implementation LocationBarSteadyView

- (instancetype)init {
  self = [super initWithFrame:CGRectZero];
  if (self) {
    _locationLabel = [[UILabel alloc] init];
    _locationIconImageView = [[UIImageView alloc] init];
    _locationIconImageView.translatesAutoresizingMaskIntoConstraints = NO;
    [_locationIconImageView
        setContentCompressionResistancePriority:UILayoutPriorityRequired
                                        forAxis:
                                            UILayoutConstraintAxisHorizontal];
    SetA11yLabelAndUiAutomationName(
        _locationIconImageView,
        IDS_IOS_PAGE_INFO_SECURITY_BUTTON_ACCESSIBILITY_LABEL,
        @"Page Security Info");
    _locationIconImageView.isAccessibilityElement = YES;

    // Setup trailing button.
    _trailingButton =
        [ExtendedTouchTargetButton buttonWithType:UIButtonTypeSystem];
    _trailingButton.translatesAutoresizingMaskIntoConstraints = NO;
    _trailingButton.pointerInteractionEnabled = YES;
    // Make the pointer shape fit the location bar's semi-circle end shape.
    _trailingButton.pointerStyleProvider =
        CreateLiftEffectCirclePointerStyleProvider();

    // Setup label.
    _locationLabel.lineBreakMode = NSLineBreakByTruncatingHead;
    _locationLabel.translatesAutoresizingMaskIntoConstraints = NO;
    [_locationLabel
        setContentCompressionResistancePriority:UILayoutPriorityDefaultLow
                                        forAxis:UILayoutConstraintAxisVertical];
    _locationLabel.font = [self locationLabelFont];

    // Container for location label and icon.
    _locationContainerView = [[UIView alloc] init];
    _locationContainerView.translatesAutoresizingMaskIntoConstraints = NO;
    _locationContainerView.userInteractionEnabled = NO;
    [_locationContainerView addSubview:_locationIconImageView];
    [_locationContainerView addSubview:_locationLabel];

    if (IsVivaldiRunning()) {
      _showLocationImageConstraints = @[
        [_locationContainerView.leadingAnchor
            constraintEqualToAnchor:_locationIconImageView.leadingAnchor],
        [_locationIconImageView.trailingAnchor
            constraintEqualToAnchor:_locationLabel.leadingAnchor
                constant:vLocationBarSteadyViewLocationImageToLabelSpacing],
        [_locationLabel.trailingAnchor
            constraintEqualToAnchor:_locationContainerView.trailingAnchor],
        [_locationIconImageView.centerYAnchor
            constraintEqualToAnchor:_locationContainerView.centerYAnchor],
      ];
    } else {
    _showLocationImageConstraints = @[
      [_locationContainerView.leadingAnchor
          constraintEqualToAnchor:_locationIconImageView.leadingAnchor],
      [_locationIconImageView.trailingAnchor
          constraintEqualToAnchor:_locationLabel.leadingAnchor
                         constant:kLocationImageToLabelSpacing],
      [_locationLabel.trailingAnchor
          constraintEqualToAnchor:_locationContainerView.trailingAnchor],
      [_locationIconImageView.centerYAnchor
          constraintEqualToAnchor:_locationContainerView.centerYAnchor],
    ];
    } // End Vivaldi

    _hideLocationImageConstraints = @[
      [_locationContainerView.leadingAnchor
          constraintEqualToAnchor:_locationLabel.leadingAnchor],
      [_locationLabel.trailingAnchor
          constraintEqualToAnchor:_locationContainerView.trailingAnchor],
    ];

    [NSLayoutConstraint activateConstraints:_showLocationImageConstraints];

    _locationButton = [[LocationBarSteadyButton alloc] init];
    _locationButton.translatesAutoresizingMaskIntoConstraints = NO;
    [_locationButton addSubview:_trailingButton];
    [_locationButton addSubview:_locationContainerView];

    [self addSubview:_locationButton];


    AddSameConstraints(self, _locationButton);

    if (IsVivaldiRunning()) {
      _locationContainerViewLeadingAnchorConstraint =
          [_locationContainerView.leadingAnchor
              constraintEqualToAnchor:self.leadingAnchor
                             constant:kLocationBarLeadingPadding];

      _trailingButtonTrailingAnchorConstraint =
          [self.trailingButton.trailingAnchor
              constraintEqualToAnchor:self.trailingAnchor
                             constant:self.trailingButtonTrailingSpacing];

      // Setup and activate constraints.
      [NSLayoutConstraint activateConstraints:@[
        [_locationLabel.centerYAnchor
            constraintEqualToAnchor:_locationContainerView.centerYAnchor],
        [_locationLabel.heightAnchor
            constraintLessThanOrEqualToAnchor:_locationContainerView.heightAnchor],
        [_trailingButton.centerYAnchor
            constraintEqualToAnchor:self.centerYAnchor],
        [_locationContainerView.centerYAnchor
            constraintEqualToAnchor:self.centerYAnchor],
        [_trailingButton.leadingAnchor
            constraintGreaterThanOrEqualToAnchor:_locationContainerView
                                                     .trailingAnchor],
        [_trailingButton.widthAnchor constraintEqualToConstant:kButtonSize],
        [_trailingButton.heightAnchor constraintEqualToConstant:kButtonSize],
        _trailingButtonTrailingAnchorConstraint,
        _locationContainerViewLeadingAnchorConstraint,
      ]];
    } else {

    // Make the label gravitate towards the center of the view.
    NSLayoutConstraint* centerX = [_locationContainerView.centerXAnchor
        constraintEqualToAnchor:self.centerXAnchor];
    centerX.priority = UILayoutPriorityDefaultHigh;

    _locationContainerViewLeadingAnchorConstraint =
        [_locationContainerView.leadingAnchor
            constraintGreaterThanOrEqualToAnchor:self.leadingAnchor
                                        constant:kLocationBarLeadingPadding];

    _trailingButtonTrailingAnchorConstraint =
        [self.trailingButton.trailingAnchor
            constraintEqualToAnchor:self.trailingAnchor
                           constant:self.trailingButtonTrailingSpacing];

    // Setup and activate constraints.
    [NSLayoutConstraint activateConstraints:@[
      [_locationLabel.centerYAnchor
          constraintEqualToAnchor:_locationContainerView.centerYAnchor
                         constant:kLocationLabelVerticalOffset],
      [_locationLabel.heightAnchor
          constraintLessThanOrEqualToAnchor:_locationContainerView.heightAnchor
                                   constant:2 * kLocationLabelVerticalOffset],
      [_trailingButton.centerYAnchor
          constraintEqualToAnchor:self.centerYAnchor],
      [_locationContainerView.centerYAnchor
          constraintEqualToAnchor:self.centerYAnchor],
      [_trailingButton.leadingAnchor
          constraintGreaterThanOrEqualToAnchor:_locationContainerView
                                                   .trailingAnchor],
      [_trailingButton.widthAnchor constraintEqualToConstant:kButtonSize],
      [_trailingButton.heightAnchor constraintEqualToConstant:kButtonSize],
      _trailingButtonTrailingAnchorConstraint,
      centerX,
      _locationContainerViewLeadingAnchorConstraint,
    ]];
    } // End Vivaldi
  }

  // Setup accessibility.
  _trailingButton.isAccessibilityElement = YES;
  _locationButton.isAccessibilityElement = YES;
  _locationButton.accessibilityLabel =
      l10n_util::GetNSString(IDS_ACCNAME_LOCATION);

  _accessibleElements = [[NSMutableArray alloc] init];
  [_accessibleElements addObject:_locationButton];
  [_accessibleElements addObject:_trailingButton];

  // These two elements must remain accessible for egtests, but will not be
  // included in accessibility navigation as they are not added to the
  // accessibleElements array.
  _locationIconImageView.isAccessibilityElement = YES;
  _locationLabel.isAccessibilityElement = YES;

  [self updateAccessibility];

  return self;
}

- (CGFloat)trailingButtonTrailingSpacing {
  if (IsSplitToolbarMode(self)) {

    if (IsVivaldiRunning())
      return vLocationBarSteadyViewShareButtonTrailingSpacing;
    // End Vivaldi

    return kShareButtonTrailingSpacing;
  } else {
    return kVoiceSearchButtonTrailingSpacing;
  }
}

- (void)setColorScheme:(LocationBarSteadyViewColorScheme*)colorScheme {
  _colorScheme = colorScheme;
  self.trailingButton.tintColor = self.colorScheme.trailingButtonColor;
  // The text color is set in -setLocationLabelText: and
  // -setLocationLabelPlaceholderText: because the two text styles have
  // different colors. The icon should be the same color as the text, but it
  // only appears with the regular label, so its color can be set here.
  self.locationIconImageView.tintColor = self.colorScheme.fontColor;
}

- (void)setLocationImage:(UIImage*)locationImage {
  BOOL hadImage = self.locationIconImageView.image != nil;
  BOOL hasImage = locationImage != nil;
  self.locationIconImageView.image = locationImage;
  if (hadImage == hasImage) {
    return;
  }

  if (hasImage) {
    [self.locationContainerView addSubview:self.locationIconImageView];
    [NSLayoutConstraint
        deactivateConstraints:self.hideLocationImageConstraints];
    [NSLayoutConstraint activateConstraints:self.showLocationImageConstraints];
  } else {
    [NSLayoutConstraint
        deactivateConstraints:self.showLocationImageConstraints];
    [NSLayoutConstraint activateConstraints:self.hideLocationImageConstraints];
    [self.locationIconImageView removeFromSuperview];
  }
}

- (void)setLocationLabelText:(NSString*)string {
  if ([self.locationLabel.text isEqualToString:string]) {
    return;
  }
  self.locationLabel.textColor = self.colorScheme.fontColor;
  self.locationLabel.text = string;
  [self updateAccessibility];
}

- (void)setLocationLabelPlaceholderText:(NSString*)string {
  self.locationLabel.textColor = self.colorScheme.placeholderColor;
  self.locationLabel.text = string;
}

- (void)setSecurityLevelAccessibilityString:(NSString*)string {
  if ([_securityLevelAccessibilityString isEqualToString:string]) {
    return;
  }
  _securityLevelAccessibilityString = [string copy];
  [self updateAccessibility];
}

- (void)setBadgeView:(UIView*)badgeView {

  // Vivaldi: We don't show badge here.
  if (IsVivaldiRunning())
    return; // End Vivaldi

  BOOL hadBadgeView = _badgeView != nil;
  _badgeView = badgeView;
  if (!hadBadgeView && badgeView) {
    _badgeView.translatesAutoresizingMaskIntoConstraints = NO;
    _badgeView.isAccessibilityElement = NO;
    [self.locationButton addSubview:_badgeView];
    // Adding InfobarBadge button as an accessibility element behind location
    // label. Thus, there should be at least one object already in
    // `accessibleElements`.
    DCHECK_GT([self.accessibleElements count], 0U);
    [self.accessibleElements insertObject:_badgeView atIndex:1];

    // Lazy init.
    self.badgeViewFullScreenEnabledConstraints = @[
      [self.badgeView.leadingAnchor
          constraintGreaterThanOrEqualToAnchor:self.leadingAnchor],
      [self.badgeView.trailingAnchor
          constraintEqualToAnchor:self.locationContainerView.leadingAnchor],
    ];

    self.badgeViewFullScreenDisabledConstraints = @[
      [self.badgeView.leadingAnchor constraintEqualToAnchor:self.leadingAnchor],
      [self.badgeView.trailingAnchor
          constraintLessThanOrEqualToAnchor:self.locationContainerView
                                                .leadingAnchor],
    ];

    [NSLayoutConstraint deactivateConstraints:@[
      self.locationContainerViewLeadingAnchorConstraint
    ]];

    [NSLayoutConstraint
        activateConstraints:
            [self.badgeViewFullScreenDisabledConstraints
                arrayByAddingObjectsFromArray:@[
                  [self.badgeView.topAnchor
                      constraintEqualToAnchor:self.topAnchor],
                  [self.badgeView.bottomAnchor
                      constraintEqualToAnchor:self.bottomAnchor],
                ]]];
  }

}

- (void)setFullScreenCollapsedMode:(BOOL)isFullScreenCollapsed {

  // Vivaldi: Since we don't show badge, we can skip this.
  if (IsVivaldiRunning())
    return; // End Vivaldi

  if (!self.badgeView) {
    return;
  }
  if (isFullScreenCollapsed) {
    [NSLayoutConstraint
        activateConstraints:self.badgeViewFullScreenEnabledConstraints];
    [NSLayoutConstraint
        deactivateConstraints:self.badgeViewFullScreenDisabledConstraints];
  } else {
    [NSLayoutConstraint
        deactivateConstraints:self.badgeViewFullScreenEnabledConstraints];
    [NSLayoutConstraint
        activateConstraints:self.badgeViewFullScreenDisabledConstraints];
  }
}

- (void)displayBadgeView:(BOOL)display animated:(BOOL)animated {

  // Vivaldi: Since we don't show badge, we can skip this.
  if (IsVivaldiRunning())
    return; // End Vivaldi

  if (display) {
    // Adding InfobarBadge button as an accessibility element behind location
    // label. Thus, there should be at least one object alreading in
    // `accessibleElements`.
    DCHECK([self.accessibleElements count] > 0);
    if ([self.accessibleElements indexOfObject:self.badgeView] == NSNotFound) {
      [self.accessibleElements insertObject:self.badgeView atIndex:1];
    }
  } else {
    [self.accessibleElements removeObject:self.badgeView];
  }
  void (^changeHiddenState)() = ^{
    self.badgeView.hidden = !display;
  };
  if (animated) {
    [UIView animateWithDuration:kbadgeViewAnimationDuration
                     animations:changeHiddenState];
  } else {
    changeHiddenState();
  }
}

- (void)enableTrailingButton:(BOOL)enabled {
  self.trailingButton.enabled = enabled;
  [self updateAccessibility];
}

#pragma mark - UIResponder

// This is needed for UIMenu
- (BOOL)canBecomeFirstResponder {
  return true;
}

#pragma mark - UIView

- (void)traitCollectionDidChange:(UITraitCollection*)previousTraitCollection {
  [super traitCollectionDidChange:previousTraitCollection];
  if (previousTraitCollection.preferredContentSizeCategory !=
      self.traitCollection.preferredContentSizeCategory) {
    self.locationLabel.font = [self locationLabelFont];
  }

  self.trailingButtonTrailingAnchorConstraint.constant =
      self.trailingButtonTrailingSpacing;
  [self layoutIfNeeded];
}

#pragma mark - UIAccessibilityContainer

- (NSArray*)accessibilityElements {
  return self.accessibleElements;
}

- (NSInteger)accessibilityElementCount {
  return self.accessibleElements.count;
}

- (id)accessibilityElementAtIndex:(NSInteger)index {
  return self.accessibleElements[index];
}

- (NSInteger)indexOfAccessibilityElement:(id)element {
  return [self.accessibleElements indexOfObject:element];
}

#pragma mark - private

- (void)updateAccessibility {
  if (self.securityLevelAccessibilityString.length > 0) {
    self.locationButton.accessibilityValue =
        [NSString stringWithFormat:@"%@ %@", self.locationLabel.text,
                                   self.securityLevelAccessibilityString];
  } else {
    self.locationButton.accessibilityValue =
        [NSString stringWithFormat:@"%@", self.locationLabel.text];
  }

  if (self.trailingButton.enabled) {
    if ([self.accessibleElements indexOfObject:self.trailingButton] ==
        NSNotFound) {
      [self.accessibleElements addObject:self.trailingButton];
    }
  } else {
    [self.accessibleElements removeObject:self.trailingButton];
  }
}

// Returns the font size for the location label.
- (UIFont*)locationLabelFont {
  return LocationBarSteadyViewFont(
      self.traitCollection.preferredContentSizeCategory);
}

#pragma mark - VIVALDI
#pragma mark - SharingPositioner

- (UIView*)sourceView {
  return self.trailingButton;
}

- (CGRect)sourceRect {
  return self.trailingButton.bounds;
}

- (void)fadeSteadyViewContentsWithAlpha:(CGFloat)alpha {
  _locationButton.alpha = alpha;
}

// End Vivaldi

@end
