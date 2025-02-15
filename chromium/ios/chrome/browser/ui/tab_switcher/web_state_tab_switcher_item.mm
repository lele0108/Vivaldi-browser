// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/tab_switcher/web_state_tab_switcher_item.h"

#import "base/memory/weak_ptr.h"
#import "components/favicon/ios/web_favicon_driver.h"
#import "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/shared/model/url/url_util.h"
#import "ios/chrome/browser/shared/ui/symbols/symbols.h"
#import "ios/chrome/browser/snapshots/snapshot_tab_helper.h"
#import "ios/chrome/browser/tabs/tab_title_util.h"
#import "ios/web/public/web_state.h"

// Vivaldi
#import "app/vivaldi_apptools.h"
#import "ios/ui/ntp/vivaldi_speed_dial_constants.h"
#import "ui/base/l10n/l10n_util_mac.h"
#import "vivaldi/ios/grit/vivaldi_ios_native_strings.h"

using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
const CGFloat kSymbolSize = 16;
}

@implementation WebStateTabSwitcherItem {
  // The web state represented by this item.
  base::WeakPtr<web::WebState> _webState;
  // The potentially prefetched snapshot for the web state.
  UIImage* _prefetchedSnapshot;
}

- (instancetype)initWithWebState:(web::WebState*)webState {
  DCHECK(webState);
  self = [super initWithIdentifier:webState->GetStableIdentifier()];
  if (self) {
    _webState = webState->GetWeakPtr();

    // chrome://newtab (NTP) tabs have no title.
    if (IsUrlNtp(webState->GetVisibleURL())) {

      if (!IsVivaldiRunning()) {
      self.hidesTitle = YES;
      } // End Vivaldi

    }

    // Vivaldi
    if (IsUrlNtp(webState->GetVisibleURL())) {
      self.title = l10n_util::GetNSString(IDS_IOS_TABS_SPEED_DIAL);
    } else {
    self.title = tab_util::GetTabTitle(webState);
    } // End Vivaldi

    self.showsActivity = webState->IsLoading();

    [[NSNotificationCenter defaultCenter]
        addObserver:self
           selector:@selector(lowMemoryWarningReceived:)
               name:UIApplicationDidReceiveMemoryWarningNotification
             object:nil];
  }
  return self;
}

#pragma mark - Image Fetching

- (void)fetchFavicon:(TabSwitcherImageFetchingCompletionBlock)completion {
  web::WebState* webState = _webState.get();
  if (!webState) {
    completion(self, nil);
    return;
  }

  // NTP tabs have special treatment.
  if (IsUrlNtp(webState->GetVisibleURL())) {
    completion(self, [self NTPFavicon]);
    return;
  }

  // Use the page favicon.
  favicon::FaviconDriver* faviconDriver =
      favicon::WebFaviconDriver::FromWebState(webState);
  // The favicon drive may be null during testing.
  if (faviconDriver) {
    gfx::Image favicon = faviconDriver->GetFavicon();
    if (!favicon.IsEmpty()) {
      completion(self, favicon.ToUIImage());
      return;
    }
  }

  // Otherwise, set a default favicon.
  completion(self, [self defaultFavicon]);
}

- (void)fetchSnapshot:(TabSwitcherImageFetchingCompletionBlock)completion {
  web::WebState* webState = _webState.get();
  if (!webState) {
    completion(self, nil);
    return;
  }

  if (_prefetchedSnapshot) {
    completion(self, _prefetchedSnapshot);
    return;
  }

  __weak __typeof(self) weakSelf = self;
  SnapshotTabHelper::FromWebState(webState)->RetrieveColorSnapshot(
      ^(UIImage* snapshot) {
        if (weakSelf) {
          completion(weakSelf, snapshot);
        }
      });
}

#pragma mark - Favicons

- (UIImage*)defaultFavicon {

  if (IsVivaldiRunning())
    return [UIImage imageNamed:vNTPSDFallbackFavicon];
  // End Vivaldi

  UIImageConfiguration* configuration = [UIImageSymbolConfiguration
      configurationWithPointSize:kSymbolSize
                          weight:UIImageSymbolWeightBold
                           scale:UIImageSymbolScaleMedium];
  return DefaultSymbolWithConfiguration(kGlobeAmericasSymbol, configuration);
}

- (UIImage*)NTPFavicon {
  // By default NTP tabs gets no favicon.

  if (IsVivaldiRunning())
    return [UIImage imageNamed:@"toolbar_menu"]; // End Vivaldi

  return nil;
}

- (void)prefetchSnapshot {
  web::WebState* webState = _webState.get();
  if (!webState) {
    return;
  }

  __weak __typeof(self) weakSelf = self;
  SnapshotTabHelper::FromWebState(webState)->RetrieveColorSnapshot(
      ^(UIImage* snapshot) {
        WebStateTabSwitcherItem* strongSelf = weakSelf;
        if (!strongSelf) {
          return;
        }
        strongSelf->_prefetchedSnapshot = snapshot;
      });
}

- (void)clearPrefetchedSnapshot {
  _prefetchedSnapshot = nil;
}

#pragma mark - Private

- (void)lowMemoryWarningReceived:(NSNotification*)notification {
  [self clearPrefetchedSnapshot];
}

@end
