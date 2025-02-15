// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/toolbar/toolbar_coordinator.h"

#import "base/mac/foundation_util.h"
#import "components/prefs/pref_service.h"
#import "ios/chrome/browser/ntp/new_tab_page_tab_helper.h"
#import "ios/chrome/browser/ntp/new_tab_page_util.h"
#import "ios/chrome/browser/prerender/prerender_service.h"
#import "ios/chrome/browser/prerender/prerender_service_factory.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/browser_state/chrome_browser_state.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/public/commands/application_commands.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/shared/public/commands/find_in_page_commands.h"
#import "ios/chrome/browser/shared/public/commands/popup_menu_commands.h"
#import "ios/chrome/browser/shared/public/commands/text_zoom_commands.h"
#import "ios/chrome/browser/shared/public/commands/toolbar_commands.h"
#import "ios/chrome/browser/shared/public/features/features.h"
#import "ios/chrome/browser/shared/ui/util/uikit_ui_util.h"
#import "ios/chrome/browser/ui/location_bar/location_bar_coordinator.h"
#import "ios/chrome/browser/ui/orchestrator/omnibox_focus_orchestrator.h"
#import "ios/chrome/browser/ui/toolbar/adaptive_toolbar_view_controller.h"
#import "ios/chrome/browser/ui/toolbar/primary_toolbar_coordinator.h"
#import "ios/chrome/browser/ui/toolbar/primary_toolbar_view_controller_delegate.h"
#import "ios/chrome/browser/ui/toolbar/public/toolbar_constants.h"
#import "ios/chrome/browser/ui/toolbar/public/toolbar_utils.h"
#import "ios/chrome/browser/ui/toolbar/secondary_toolbar_coordinator.h"
#import "ios/chrome/browser/ui/toolbar/toolbar_coordinatee.h"
#import "ios/chrome/browser/ui/toolbar/toolbar_mediator.h"
#import "ios/components/webui/web_ui_url_constants.h"

// Vivaldi
#import "app/vivaldi_apptools.h"
#import "ios/chrome/browser/ui/location_bar/location_bar_steady_view_consumer.h"

using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface ToolbarCoordinator () <PrimaryToolbarCoordinatorDelegate,
                                  PrimaryToolbarViewControllerDelegate,
                                  ToolbarCommands,

                                  // Vivaldi
                                  LocationBarSteadyViewConsumer,
                                  // End Vivaldi

                                  ToolbarMediatorDelegate> {
  PrerenderService* _prerenderService;
}

/// Whether this coordinator has been started.
@property(nonatomic, assign) BOOL started;
/// Coordinator for the location bar containing the omnibox.
@property(nonatomic, strong) LocationBarCoordinator* locationBarCoordinator;
/// Coordinator for the primary toolbar at the top of the screen.
@property(nonatomic, strong)
    PrimaryToolbarCoordinator* primaryToolbarCoordinator;
/// Coordinator for the secondary toolbar at the bottom of the screen.
@property(nonatomic, strong)
    SecondaryToolbarCoordinator* secondaryToolbarCoordinator;

/// Mediator observing WebStateList for toolbars.
@property(nonatomic, strong) ToolbarMediator* toolbarMediator;
/// Orchestrator for the omnibox focus animation.
@property(nonatomic, strong) OmniboxFocusOrchestrator* orchestrator;
/// Whether the omnibox is currently focused.
@property(nonatomic, assign) BOOL locationBarFocused;
/// Whether the omnibox focusing should happen with animation.
@property(nonatomic, assign) BOOL enableAnimationsForOmniboxFocus;

@end

@implementation ToolbarCoordinator {
  /// Type of toolbar containing the omnibox.
  ToolbarType _omniboxPosition;
}

- (instancetype)initWithBrowser:(Browser*)browser {
  CHECK(browser);
  self = [super initWithBaseViewController:nil browser:browser];
  if (self) {
    // Initialize both coordinators here as they might be referenced before
    // `start`.
    _primaryToolbarCoordinator =
        [[PrimaryToolbarCoordinator alloc] initWithBrowser:browser];
    _secondaryToolbarCoordinator =
        [[SecondaryToolbarCoordinator alloc] initWithBrowser:browser];

    [self.browser->GetCommandDispatcher()
        startDispatchingToTarget:self
                     forProtocol:@protocol(ToolbarCommands)];
  }
  return self;
}

- (void)start {
  if (self.started) {
    return;
  }
  self.enableAnimationsForOmniboxFocus = YES;
  // Set a default position, overriden by `setInitialOmniboxPosition` below.
  _omniboxPosition = ToolbarType::kPrimary;

  Browser* browser = self.browser;
  [browser->GetCommandDispatcher()
      startDispatchingToTarget:self
                   forProtocol:@protocol(FakeboxFocuser)];

  PrefService* prefs =
      ChromeBrowserState::FromBrowserState(browser->GetBrowserState())
          ->GetPrefs();
  self.toolbarMediator = [[ToolbarMediator alloc]
      initWithWebStateList:browser->GetWebStateList()
               isIncognito:browser->GetBrowserState()->IsOffTheRecord()];
  self.toolbarMediator.delegate = self;
  self.toolbarMediator.prefService = prefs;

  self.locationBarCoordinator =
      [[LocationBarCoordinator alloc] initWithBrowser:browser];
  self.locationBarCoordinator.delegate = self.omniboxFocusDelegate;
  self.locationBarCoordinator.popupPresenterDelegate =
      self.popupPresenterDelegate;

  // Vivaldi
  self.locationBarCoordinator.steadyViewConsumer = self;
  // End Vivaldi

  [self.locationBarCoordinator start];

  self.primaryToolbarCoordinator.delegate = self;
  self.primaryToolbarCoordinator.viewControllerDelegate = self;
  [self.primaryToolbarCoordinator start];
  [self.secondaryToolbarCoordinator start];

  self.orchestrator = [[OmniboxFocusOrchestrator alloc] init];
  self.orchestrator.toolbarAnimatee =
      self.primaryToolbarCoordinator.toolbarAnimatee;
  self.orchestrator.locationBarAnimatee =
      [self.locationBarCoordinator locationBarAnimatee];
  self.orchestrator.editViewAnimatee =
      [self.locationBarCoordinator editViewAnimatee];

  if (IsBottomOmniboxSteadyStateEnabled()) {
    [self.toolbarMediator setInitialOmniboxPosition];
  } else {
    [self.primaryToolbarCoordinator
        setLocationBarViewController:self.locationBarCoordinator
                                         .locationBarViewController];
  }

  [self updateToolbarsLayout];
  _prerenderService = PrerenderServiceFactory::GetForBrowserState(
      self.browser->GetBrowserState());

  [super start];
  self.started = YES;
}

- (void)stop {
  if (!self.started) {
    return;
  }
  [super stop];

  [self.toolbarMediator disconnect];
  self.toolbarMediator = nil;

  // Vivaldi
  self.locationBarCoordinator.steadyViewConsumer = nil;
  // End Vivaldi

  [self.locationBarCoordinator stop];
  self.locationBarCoordinator = nil;
  [self.primaryToolbarCoordinator stop];
  [self.secondaryToolbarCoordinator stop];

  [self.browser->GetCommandDispatcher() stopDispatchingToTarget:self];
  _prerenderService = nullptr;
  self.started = NO;
}

#pragma mark - Public

- (UIViewController*)primaryToolbarViewController {
  return self.primaryToolbarCoordinator.viewController;
}

- (UIViewController*)secondaryToolbarViewController {
  return self.secondaryToolbarCoordinator.viewController;
}

- (id<SharingPositioner>)sharingPositioner {

  // Vivaldi: We will return location bar here since share button is within
  // location bar for us.
  if (IsVivaldiRunning())
      return [self.locationBarCoordinator vivaldiPositioner];
  // End Vivaldi

  return self.primaryToolbarCoordinator.SharingPositioner;
}

// Public and in `ToolbarMediatorDelegate`.
- (void)updateToolbar {
  web::WebState* webState =
      self.browser->GetWebStateList()->GetActiveWebState();
  if (!webState) {
    return;
  }

  // Please note, this notion of isLoading is slightly different from WebState's
  // IsLoading().
  BOOL isToolbarLoading =
      webState->IsLoading() &&
      !webState->GetLastCommittedURL().SchemeIs(kChromeUIScheme);

  if (self.isLoadingPrerenderer && isToolbarLoading) {
    [self.primaryToolbarCoordinator showPrerenderingAnimation];
  }

  id<FindInPageCommands> findInPageCommandsHandler = HandlerForProtocol(
      self.browser->GetCommandDispatcher(), FindInPageCommands);
  [findInPageCommandsHandler showFindUIIfActive];

  id<TextZoomCommands> textZoomCommandsHandler = HandlerForProtocol(
      self.browser->GetCommandDispatcher(), TextZoomCommands);
  [textZoomCommandsHandler showTextZoomUIIfActive];

  // There are times when the NTP can be hidden but before the visibleURL
  // changes.  This can leave the BVC in a blank state where only the bottom
  // toolbar is visible. Instead, if possible, use the NewTabPageTabHelper
  // IsActive() value rather than checking -IsVisibleURLNewTabPage.
  NewTabPageTabHelper* NTPHelper = NewTabPageTabHelper::FromWebState(webState);
  BOOL isNTP = NTPHelper && NTPHelper->IsActive();
  BOOL isOffTheRecord = self.browser->GetBrowserState()->IsOffTheRecord();
  BOOL canShowTabStrip = IsRegularXRegularSizeClass(self.traitEnvironment);

  // Hide the toolbar when displaying content suggestions without the tab
  // strip, without the focused omnibox, and for UI Refresh, only when in
  // split toolbar mode.
  BOOL hideToolbar = isNTP && !isOffTheRecord &&
                     ![self isOmniboxFirstResponder] &&
                     ![self showingOmniboxPopup] && !canShowTabStrip &&
                     IsSplitToolbarMode(self.traitEnvironment);

  if (IsVivaldiRunning()) {
    self.primaryToolbarViewController.view.hidden = NO;
  } else {
  self.primaryToolbarViewController.view.hidden = hideToolbar;
  } // End Vivaldi

}

- (BOOL)isLoadingPrerenderer {
  return _prerenderService && _prerenderService->IsLoadingPrerender();
}

#pragma mark Omnibox and LocationBar

- (void)transitionToLocationBarFocusedState:(BOOL)focused {
  if (self.traitEnvironment.traitCollection.verticalSizeClass ==
      UIUserInterfaceSizeClassUnspecified) {
    return;
  }
  [self.toolbarMediator locationBarFocusChangedTo:focused];

  [self.orchestrator
      transitionToStateOmniboxFocused:focused
                      toolbarExpanded:focused && !IsRegularXRegularSizeClass(
                                                     self.traitEnvironment)
                             animated:self.enableAnimationsForOmniboxFocus];
  self.locationBarFocused = focused;
}

- (BOOL)isOmniboxFirstResponder {
  return [self.locationBarCoordinator isOmniboxFirstResponder];
}

- (BOOL)showingOmniboxPopup {
  return [self.locationBarCoordinator showingOmniboxPopup];
}

#pragma mark SnapshotProviding

- (id<SideSwipeToolbarSnapshotProviding>)primaryToolbarSnapshotProvider {
  return self.primaryToolbarCoordinator;
}

- (id<SideSwipeToolbarSnapshotProviding>)secondaryToolbarSnapshotProvider {
  return self.secondaryToolbarCoordinator;
}

#pragma mark ToolbarHeightProviding

- (CGFloat)collapsedPrimaryToolbarHeight {
  if (_omniboxPosition == ToolbarType::kSecondary) {
    CHECK(IsBottomOmniboxSteadyStateEnabled());
    // TODO(crbug.com/1455030): Return 0 here once overlay message is fixed.
    // Currently, it's in a infinite loop when we try to show a message with a
    // non-expanded primary toolbar.
    return self.expandedPrimaryToolbarHeight;
  }

  return ToolbarCollapsedHeight(
      self.traitEnvironment.traitCollection.preferredContentSizeCategory);
}

- (CGFloat)expandedPrimaryToolbarHeight {
  if (_omniboxPosition == ToolbarType::kSecondary) {
    CHECK(IsBottomOmniboxSteadyStateEnabled());
    // TODO(crbug.com/1455030): Return 0 here once overlay message is fixed.
    // Currently, it's in a infinite loop when we try to show a message with a
    // non-expanded primary toolbar.
  }

  CGFloat height =
      self.primaryToolbarViewController.view.intrinsicContentSize.height;
  if (!IsSplitToolbarMode(self.traitEnvironment)) {
    // When the adaptive toolbar is unsplit, add a margin.
    height += kTopToolbarUnsplitMargin;
  }
  return height;
}

- (CGFloat)collapsedSecondaryToolbarHeight {
  if (_omniboxPosition == ToolbarType::kSecondary) {
    CHECK(IsBottomOmniboxSteadyStateEnabled());
    return ToolbarCollapsedHeight(
        self.traitEnvironment.traitCollection.preferredContentSizeCategory);
  }
  return 0.0;
}

- (CGFloat)expandedSecondaryToolbarHeight {
  if (!IsSplitToolbarMode(self.traitEnvironment)) {
    return 0.0;
  }
  CGFloat height =
      self.secondaryToolbarViewController.view.intrinsicContentSize.height;
  if (_omniboxPosition == ToolbarType::kSecondary) {
    CHECK(IsBottomOmniboxSteadyStateEnabled());
    height += kSecondaryToolbarOmniboxHeight;
  }
  return height;
}

#pragma mark ViewRevealing

- (id<ViewRevealingAnimatee>)viewRevealingAnimatee {
  CHECK(self.primaryToolbarCoordinator.animatee);
  return self.primaryToolbarCoordinator.animatee;
}

- (void)setPanGestureHandler:
    (ViewRevealingVerticalPanHandler*)panGestureHandler {
  [self.primaryToolbarCoordinator setPanGestureHandler:panGestureHandler];
}

#pragma mark - FakeboxFocuser

- (void)focusOmniboxNoAnimation {
  self.enableAnimationsForOmniboxFocus = NO;
  [self fakeboxFocused];
  self.enableAnimationsForOmniboxFocus = YES;
  // If the pasteboard is containing a URL, the omnibox popup suggestions are
  // displayed as soon as the omnibox is focused.
  // If the fake omnibox animation is triggered at the same time, it is possible
  // to see the NTP going up where the real omnibox should be displayed.
  if ([self.locationBarCoordinator omniboxPopupHasAutocompleteResults]) {
    [self onFakeboxAnimationComplete];
  }
}

- (void)fakeboxFocused {
  [self.locationBarCoordinator focusOmniboxFromFakebox];
}

- (void)onFakeboxBlur {

  if (IsVivaldiRunning()) {
    self.primaryToolbarViewController.view.hidden = NO;
  } else {
  // Hide the toolbar if the NTP is currently displayed.
  web::WebState* webState =
      self.browser->GetWebStateList()->GetActiveWebState();
  if (webState && IsVisibleURLNewTabPage(webState)) {
    self.primaryToolbarViewController.view.hidden =
        IsSplitToolbarMode(self.traitEnvironment);
  }
  } // End Vivaldi

}

- (void)onFakeboxAnimationComplete {
  self.primaryToolbarViewController.view.hidden = NO;
}

#pragma mark - NewTabPageControllerDelegate

- (void)setScrollProgressForTabletOmnibox:(CGFloat)progress {
  for (id<NewTabPageControllerDelegate> coordinator in self.coordinators) {
    [coordinator setScrollProgressForTabletOmnibox:progress];
  }
}

- (UIResponder<UITextInput>*)fakeboxScribbleForwardingTarget {
  return self.locationBarCoordinator.omniboxScribbleForwardingTarget;
}

#pragma mark - PopupMenuUIUpdating

- (void)updateUIForOverflowMenuIPHDisplayed {
  for (id<ToolbarCoordinatee> coordinator in self.coordinators) {
    [coordinator.popupMenuUIUpdater updateUIForOverflowMenuIPHDisplayed];
  }
}

- (void)updateUIForIPHDismissed {
  for (id<ToolbarCoordinatee> coordinator in self.coordinators) {
    [coordinator.popupMenuUIUpdater updateUIForIPHDismissed];
  }
}

#pragma mark - PrimaryToolbarCoordinatorDelegate

- (void)updateToolbarForSideSwipeSnapshot:(web::WebState*)webState {

  if (IsVivaldiRunning()) {
    [self.locationBarCoordinator.locationBarViewController.view setHidden:NO];
  } else {
  BOOL isNTP = IsVisibleURLNewTabPage(webState);

  // Don't do anything for a live non-ntp tab.
  if (webState == self.browser->GetWebStateList()->GetActiveWebState() &&
      !isNTP) {
    [self.locationBarCoordinator.locationBarViewController.view setHidden:NO];
  } else {
    self.primaryToolbarViewController.view.hidden = NO;
    [self.locationBarCoordinator.locationBarViewController.view setHidden:YES];
  }
  } // End Vivaldi

}

- (void)resetToolbarAfterSideSwipeSnapshot {
  [self.locationBarCoordinator.locationBarViewController.view setHidden:NO];
}

#pragma mark - PrimaryToolbarViewControllerDelegate

- (void)viewControllerTraitCollectionDidChange:
    (UITraitCollection*)previousTraitCollection {
  [self.toolbarMediator
      toolbarTraitCollectionChangedTo:self.traitEnvironment.traitCollection];
  [self updateToolbarsLayout];
}

- (void)close {
  if (self.locationBarFocused) {
    id<ApplicationCommands> applicationCommandsHandler = HandlerForProtocol(
        self.browser->GetCommandDispatcher(), ApplicationCommands);
    [applicationCommandsHandler dismissModalDialogsWithCompletion:nil];
  }
}

#pragma mark - SideSwipeToolbarInteracting

- (BOOL)isInsideToolbar:(CGPoint)point {
  for (id<ToolbarCoordinatee> coordinator in self.coordinators) {
    // The toolbar frame is inset by -1 because CGRectContainsPoint does
    // include points on the max X and Y edges, which will happen frequently
    // with edge swipes from the right side.
    CGRect toolbarFrame =
        CGRectInset([coordinator viewController].view.bounds, -1, -1);
    CGPoint pointInToolbarCoordinates =
        [[coordinator viewController].view convertPoint:point fromView:nil];
    if (CGRectContainsPoint(toolbarFrame, pointInToolbarCoordinates)) {
      return YES;
    }
  }
  return NO;
}

#pragma mark - ToolbarCommands

- (void)triggerToolbarSlideInAnimation {
  for (id<ToolbarCommands> coordinator in self.coordinators) {
    [coordinator triggerToolbarSlideInAnimation];
  }
}

- (void)setTabGridButtonIPHHighlighted:(BOOL)iphHighlighted {
  for (id<ToolbarCommands> coordinator in self.coordinators) {
    [coordinator setTabGridButtonIPHHighlighted:iphHighlighted];
  }
}

- (void)setNewTabButtonIPHHighlighted:(BOOL)iphHighlighted {
  for (id<ToolbarCommands> coordinator in self.coordinators) {
    [coordinator setNewTabButtonIPHHighlighted:iphHighlighted];
  }
}

#pragma mark - ToolbarMediatorDelegate

- (void)transitionOmniboxToToolbarType:(ToolbarType)toolbarType {
  _omniboxPosition = toolbarType;
  switch (toolbarType) {
    case ToolbarType::kPrimary:
      [self.primaryToolbarCoordinator
          setLocationBarViewController:self.locationBarCoordinator
                                           .locationBarViewController];
      [self.secondaryToolbarCoordinator setLocationBarViewController:nil];
      break;
    case ToolbarType::kSecondary:
      [self.secondaryToolbarCoordinator
          setLocationBarViewController:self.locationBarCoordinator
                                           .locationBarViewController];
      [self.primaryToolbarCoordinator setLocationBarViewController:nil];
      break;
  }
  [self.toolbarHeightDelegate toolbarsHeightChanged];
}

#pragma mark - Private

/// Returns primary and secondary coordinator in a array. Helper to call method
/// on both coordinators.
- (NSArray<id<ToolbarCoordinatee>>*)coordinators {
  return @[ self.primaryToolbarCoordinator, self.secondaryToolbarCoordinator ];
}

/// Returns the trait environment of the toolbars.
- (id<UITraitEnvironment>)traitEnvironment {
  return self.primaryToolbarViewController;
}

/// Updates toolbars layout whith current omnibox focus state.
- (void)updateToolbarsLayout {
  BOOL omniboxFocused =
      self.isOmniboxFirstResponder || self.showingOmniboxPopup;
  [self.orchestrator
      transitionToStateOmniboxFocused:omniboxFocused
                      toolbarExpanded:omniboxFocused &&
                                      !IsRegularXRegularSizeClass(
                                          self.traitEnvironment)
                             animated:NO];
}

#pragma mark - VIVALDI
#pragma mark - LocationBarSteadyViewConsumer

- (void)updateLocationText:(NSString*)text clipTail:(BOOL)clipTail {
  [self.steadyViewConsumer updateLocationText:text
                                     clipTail:clipTail];
}

- (void)updateLocationIcon:(UIImage*)icon
        securityStatusText:(NSString*)statusText {
  [self.steadyViewConsumer updateLocationIcon:icon
                           securityStatusText:statusText];
}

- (void)updateAfterNavigatingToNTP {
  // no op.
}

- (void)updateLocationShareable:(BOOL)shareable {
  // no op.
}

@end
