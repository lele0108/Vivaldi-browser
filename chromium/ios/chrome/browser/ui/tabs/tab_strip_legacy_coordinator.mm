// Copyright 2017 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/tabs/tab_strip_legacy_coordinator.h"

#import "ios/chrome/browser/shared/coordinator/layout_guide/layout_guide_util.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/shared/public/commands/tab_strip_commands.h"
#import "ios/chrome/browser/ui/tabs/requirements/tab_strip_presentation.h"
#import "ios/chrome/browser/ui/tabs/tab_strip_controller.h"

// Vivaldi
#import "ios/ui/settings/tabs/vivaldi_tab_setting_prefs.h"
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@protocol TabStripContaining;

@interface TabStripLegacyCoordinator ()
@property(nonatomic, assign) BOOL started;
@property(nonatomic, strong) TabStripController* tabStripController;
@end

@implementation TabStripLegacyCoordinator
@synthesize presentationProvider = _presentationProvider;
@synthesize started = _started;
@synthesize tabStripController = _tabStripController;
@synthesize animationWaitDuration = _animationWaitDuration;
@synthesize baseViewController = _baseViewController;

- (instancetype)initWithBrowser:(Browser*)browser {
  DCHECK(browser);
  return [super initWithBaseViewController:nil browser:browser];
}

- (UIView<TabStripContaining>*)view {
  DCHECK(self.started);
  return [self.tabStripController view];
}

- (void)setPresentationProvider:(id<TabStripPresentation>)presentationProvider {
  DCHECK(!self.started);
  _presentationProvider = presentationProvider;
}

- (void)setAnimationWaitDuration:(NSTimeInterval)animationWaitDuration {
  DCHECK(!self.started);
  _animationWaitDuration = animationWaitDuration;
}

- (void)setHighlightsSelectedTab:(BOOL)highlightsSelectedTab {
  DCHECK(self.started);
  self.tabStripController.highlightsSelectedTab = highlightsSelectedTab;
}

- (void)hideTabStrip:(BOOL)hidden {
  [self.tabStripController hideTabStrip:hidden];
}

- (void)tabStripSizeDidChange {
  [self.tabStripController tabStripSizeDidChange];
}

- (void)setPanGestureHandler:
    (ViewRevealingVerticalPanHandler*)panGestureHandler {
  self.tabStripController.panGestureHandler = panGestureHandler;
}

- (id<ViewRevealingAnimatee>)animatee {
  return self.tabStripController.animatee;
}

#pragma mark - ChromeCoordinator

- (void)start {
  DCHECK(self.browser);
  DCHECK(self.presentationProvider);
  TabStripStyle style =
      self.browser->GetBrowserState()->IsOffTheRecord() ? INCOGNITO : NORMAL;
  self.tabStripController = [[TabStripController alloc]
      initWithBaseViewController:self.baseViewController
                         browser:self.browser
                           style:style
               layoutGuideCenter:LayoutGuideCenterForBrowser(self.browser)];
  self.tabStripController.presentationProvider = self.presentationProvider;
  self.tabStripController.animationWaitDuration = self.animationWaitDuration;
  [self.presentationProvider showTabStripView:[self.tabStripController view]];
  [self.browser->GetCommandDispatcher()
      startDispatchingToTarget:_tabStripController
                   forProtocol:@protocol(TabStripCommands)];
  self.started = YES;
}

- (void)stop {
  self.started = NO;
  [self.tabStripController disconnect];
  self.tabStripController = nil;
  self.presentationProvider = nil;
  [self.browser->GetCommandDispatcher()
      stopDispatchingForProtocol:@protocol(TabStripCommands)];
}

#pragma mark VIVALDI

- (BOOL)isTabStackEnabled {
  if (!self.browser)
    return NO;

  return [VivaldiTabSettingPrefs getUseTabStackWithPrefService:
          self.browser->GetBrowserState()->GetPrefs()];
}

- (void)scrollToSelectedTab:(web::WebState*)webState
                   animated:(BOOL)animated {
  [self.tabStripController scrollToSelectedTab:webState
                                      animated:animated];
}

@end
