// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/ntp/incognito/incognito_view_controller.h"

#import <string>

#import "components/content_settings/core/common/features.h"
#import "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/ui/ntp/incognito/incognito_view.h"
#import "ios/chrome/browser/ui/ntp/incognito/revamped_incognito_view.h"
#import "ios/chrome/browser/ui/ntp/new_tab_page_constants.h"
#import "ios/chrome/browser/ui/ntp/new_tab_page_url_loader_delegate.h"
#import "ios/chrome/browser/url_loading/url_loading_browser_agent.h"
#import "ios/chrome/browser/url_loading/url_loading_params.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"

// Vivaldi
#import "app/vivaldi_apptools.h"
#import "ios/ui/helpers/vivaldi_uiview_layout_helper.h"
#import "ios/ui/ntp/vivaldi_ntp_constants.h"
#import "ios/ui/ntp/vivaldi_private_mode_view.h"

using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface IncognitoViewController () <NewTabPageURLLoaderDelegate>

// The scrollview containing the actual views.
@property(nonatomic, strong) UIScrollView* incognitoView;

@end

@implementation IncognitoViewController {
  // The UrlLoadingService associated with this view.
  // TODO(crbug.com/1335402): View controllers should not have access to
  // model-layer objects. Create a mediator to connect model-layer class
  // `UrlLoadingBrowserAgent` to the view controller.
  UrlLoadingBrowserAgent* _URLLoader;  // weak
}

- (instancetype)initWithUrlLoader:(UrlLoadingBrowserAgent*)URLLoader {
  self = [super init];
  if (self) {
    _URLLoader = URLLoader;
  }
  return self;
}

- (void)viewDidLoad {
  self.overrideUserInterfaceStyle = UIUserInterfaceStyleDark;

  if (IsVivaldiRunning()) {
    UIImageView* bgView = [UIImageView new];
    bgView.contentMode = UIViewContentModeScaleAspectFill;
    bgView.image = [UIImage imageNamed:vNTPPrivateTabBG];
    bgView.backgroundColor = UIColor.clearColor;
    [self.view addSubview:bgView];
    [bgView fillSuperview];

    VivaldiPrivateModeView* privateView = [VivaldiPrivateModeView new];
    [self.view addSubview:privateView];
    [privateView fillSuperview];
    privateView.URLLoaderDelegate = self;
  } else {
  if (base::FeatureList::IsEnabled(kIncognitoNtpRevamp)) {
    RevampedIncognitoView* view =
        [[RevampedIncognitoView alloc] initWithFrame:self.view.bounds];
    view.URLLoaderDelegate = self;
    self.incognitoView = view;
  } else {
    IncognitoView* view =
        [[IncognitoView alloc] initWithFrame:self.view.bounds];
    view.URLLoaderDelegate = self;
    self.incognitoView = view;
  }

  self.incognitoView.accessibilityIdentifier = kNTPIncognitoViewIdentifier;
  [self.incognitoView setAutoresizingMask:UIViewAutoresizingFlexibleHeight |
                                          UIViewAutoresizingFlexibleWidth];
  self.incognitoView.backgroundColor = [UIColor colorNamed:kBackgroundColor];
  [self.view addSubview:self.incognitoView];
  } // End Vivaldi

}

- (void)dealloc {
  [_incognitoView setDelegate:nil];
}

#pragma mark - NewTabPageURLLoaderDelegate

- (void)loadURLInTab:(const GURL&)URL {
  _URLLoader->Load(UrlLoadParams::InCurrentTab(URL));
}

@end
