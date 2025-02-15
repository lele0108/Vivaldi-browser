// Copyright 2018 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/tab_switcher/tab_grid/tab_grid_new_tab_button.h"

#import "base/check.h"
#import "base/notreached.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/ui/tab_switcher/tab_grid/tab_grid_constants.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/common/ui/util/constraints_ui_util.h"
#import "ios/chrome/common/ui/util/pointer_interaction_util.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util.h"

// Vivaldi
#import "app/vivaldi_apptools.h"

using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// The size of the small symbol image.
const CGFloat kSmallSymbolSize = 24;
// The size of the large symbol image.
const CGFloat kLargeSymbolSize = 37;

}  // namespace

@interface TabGridNewTabButton ()

// Images for the open new tab button.
@property(nonatomic, strong) UIImage* regularImage;
@property(nonatomic, strong) UIImage* incognitoImage;

@property(nonatomic, strong) UIImage* symbol;

@end

@implementation TabGridNewTabButton

- (instancetype)initWithLargeSize:(BOOL)largeSize {
  self = [super initWithFrame:CGRectZero];
  if (self) {
    CGFloat symbolSize = largeSize ? kLargeSymbolSize : kSmallSymbolSize;
    if (base::FeatureList::IsEnabled(kSFSymbolsFollowUp)) {
      _symbol = CustomSymbolWithPointSize(kPlusCircleFillSymbol, symbolSize);
    } else {
      _symbol =
          CustomSymbolWithPointSize(kLegacyPlusCircleFillSymbol, symbolSize);
    }
    [self setImage:_symbol forState:UIControlStateNormal];
    self.pointerInteractionEnabled = YES;
    self.pointerStyleProvider = CreateLiftEffectCirclePointerStyleProvider();
  }
  return self;
}

- (instancetype)initWithRegularImage:(UIImage*)regularImage
                      incognitoImage:(UIImage*)incognitoImage {
  self = [super initWithFrame:CGRectZero];
  if (self) {

    if (!IsVivaldiRunning()) {
    if (@available(iOS 15, *)) {
      NOTREACHED();
    }
    } // End Vivaldi

    _regularImage = regularImage;
    _incognitoImage = incognitoImage;

    self.pointerInteractionEnabled = YES;
    self.pointerStyleProvider = CreateLiftEffectCirclePointerStyleProvider();
  }
  return self;
}

#pragma mark - Public

- (void)setPage:(TabGridPage)page {

  if (IsVivaldiRunning()) {
    [self setIconPage:page];
  } else {
  if (@available(iOS 15, *)) {
    [self setSymbolPage:page];
  } else {
    [self setIconPage:page];
  }
  } // End Vivaldi

}

#pragma mark - Private

// Sets page using icon images.
- (void)setIconPage:(TabGridPage)page {

  if (!IsVivaldiRunning()) {
  if (@available(iOS 15, *)) {
    NOTREACHED();
  }
  } // End Vivaldi

  // self.page is inited to 0 (i.e. TabGridPageIncognito) so do not early return
  // here, otherwise when app is launched in incognito mode the image will be
  // missing.
  UIImage* renderedImage;
  switch (page) {
    case TabGridPageIncognitoTabs:
      self.accessibilityLabel =
          l10n_util::GetNSString(IDS_IOS_TAB_GRID_CREATE_NEW_INCOGNITO_TAB);
      renderedImage = [_incognitoImage
          imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal];
      break;
    case TabGridPageRegularTabs:
      self.accessibilityLabel =
          l10n_util::GetNSString(IDS_IOS_TAB_GRID_CREATE_NEW_TAB);
      renderedImage = [_regularImage
          imageWithRenderingMode:UIImageRenderingModeAlwaysOriginal];
      break;
    case TabGridPageRemoteTabs:

    // Vivaldi
    case TabGridPageClosedTabs:
    // End Vivaldi

      break;
  }
  _page = page;

  if (IsVivaldiRunning()) {
    renderedImage = [renderedImage
        imageWithRenderingMode:UIImageRenderingModeAlwaysTemplate];
  } // End Vivaldi

  [self setImage:renderedImage forState:UIControlStateNormal];
}

// Sets page using a symbol image.
- (void)setSymbolPage:(TabGridPage)page API_AVAILABLE(ios(15)) {
  switch (page) {
    case TabGridPageIncognitoTabs:
      self.accessibilityLabel =
          l10n_util::GetNSString(IDS_IOS_TAB_GRID_CREATE_NEW_INCOGNITO_TAB);
      [self
          setImage:SymbolWithPalette(
                       self.symbol, @[ UIColor.blackColor, UIColor.whiteColor ])
          forState:UIControlStateNormal];
      break;
    case TabGridPageRegularTabs:
      self.accessibilityLabel =
          l10n_util::GetNSString(IDS_IOS_TAB_GRID_CREATE_NEW_TAB);
      [self
          setImage:SymbolWithPalette(self.symbol,
                                     @[
                                       UIColor.blackColor,
                                       [UIColor colorNamed:kStaticBlue400Color]
                                     ])
          forState:UIControlStateNormal];
      break;
    case TabGridPageRemoteTabs:

    // Vivaldi
    case TabGridPageClosedTabs:
    // End Vivaldi

      break;
  }
  _page = page;
}

@end
