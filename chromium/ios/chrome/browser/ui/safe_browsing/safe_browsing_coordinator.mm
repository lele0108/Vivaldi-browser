// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/safe_browsing/safe_browsing_coordinator.h"

#import "base/feature_list.h"
#import "components/safe_browsing/core/common/features.h"
#import "ios/chrome/browser/shared/model/browser/browser.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list.h"
#import "ios/chrome/browser/shared/model/web_state_list/web_state_list_observer_bridge.h"
#import "ios/chrome/browser/shared/public/commands/application_commands.h"
#import "ios/chrome/browser/shared/public/commands/command_dispatcher.h"
#import "ios/chrome/browser/ui/settings/privacy/privacy_safe_browsing_coordinator.h"
#import "ios/components/security_interstitials/safe_browsing/safe_browsing_tab_helper.h"
#import "ios/components/security_interstitials/safe_browsing/safe_browsing_tab_helper_delegate.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

@interface SafeBrowsingCoordinator () <SafeBrowsingTabHelperDelegate,
                                       WebStateListObserving> {
  std::unique_ptr<WebStateListObserver> _webStateListObserver;
}

// The WebStateList that this mediator listens for any changes on the active web
// state.
@property(nonatomic, readonly) WebStateList* webStateList;

@end

@implementation SafeBrowsingCoordinator

- (instancetype)initWithBaseViewController:(UIViewController*)viewController
                                   browser:(Browser*)browser {
  self = [super initWithBaseViewController:viewController browser:browser];
  if (self) {
    _webStateList = browser->GetWebStateList();
    for (int i = 0; i < _webStateList->count(); i++) {
      web::WebState* web_state = _webStateList->GetWebStateAt(i);
      SafeBrowsingTabHelper::FromWebState(web_state)->SetDelegate(self);
    }
    _webStateListObserver = std::make_unique<WebStateListObserverBridge>(self);
    _webStateList->AddObserver(_webStateListObserver.get());
  }
  return self;
}

- (void)dealloc {
  if (_webStateList) {
    _webStateList->RemoveObserver(_webStateListObserver.get());
    _webStateListObserver = nullptr;
    _webStateList = nullptr;
  }
}

#pragma mark - SafeBrowsingTabHelperDelegate

- (void)openSafeBrowsingSettings {
  id<ApplicationCommands> applicationHandler = HandlerForProtocol(
      self.browser->GetCommandDispatcher(), ApplicationCommands);
  [applicationHandler showSafeBrowsingSettings];
}

#pragma mark - WebStateListObserving

- (void)didChangeWebStateList:(WebStateList*)webStateList
                       change:(const WebStateListChange&)change
                    selection:(const WebStateSelection&)selection {
  switch (change.type()) {
    case WebStateListChange::Type::kSelectionOnly:
      // Do nothing when a WebState is selected and its status is updated.
      break;
    case WebStateListChange::Type::kDetach:
      // TODO(crbug.com/1442546): Move the implementation from
      // webStateList:didDetachWebState:atIndex: to here.
      break;
    case WebStateListChange::Type::kMove:
      // Do nothing when a WebState is moved.
      break;
    case WebStateListChange::Type::kReplace: {
      const WebStateListChangeReplace& replaceChange =
          change.As<WebStateListChangeReplace>();
      SafeBrowsingTabHelper::FromWebState(replaceChange.inserted_web_state())
          ->SetDelegate(self);
      break;
    }
    case WebStateListChange::Type::kInsert: {
      const WebStateListChangeInsert& insertChange =
          change.As<WebStateListChangeInsert>();
      SafeBrowsingTabHelper::FromWebState(insertChange.inserted_web_state())
          ->SetDelegate(self);
      break;
    }
  }
}

- (void)webStateList:(WebStateList*)webStateList
    didDetachWebState:(web::WebState*)webState
              atIndex:(int)atIndex {
  SafeBrowsingTabHelper::FromWebState(webState)->RemoveDelegate();
}

@end
