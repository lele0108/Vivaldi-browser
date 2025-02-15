// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef IOS_CHROME_BROWSER_UI_WHATS_NEW_WHATS_NEW_UTIL_H_
#define IOS_CHROME_BROWSER_UI_WHATS_NEW_WHATS_NEW_UTIL_H_

#import <Foundation/Foundation.h>
#import "base/feature_list.h"
#import "ios/chrome/browser/ui/whats_new/data_source/whats_new_item.h"

class PromosManager;

// Feature flag that enables version 2 of What's New.
BASE_DECLARE_FEATURE(kWhatsNewIOSM116);

// Key to store whether the What's New promo has been register.
extern NSString* const kWhatsNewPromoRegistrationKey;

// Key to store whether the What's New m116 promo has been register.
extern NSString* const kWhatsNewM116PromoRegistrationKey;

// Key to store the date of FRE.
extern NSString* const kWhatsNewDaysAfterFre;

// Key to store the date of FRE for What's New M116.
extern NSString* const kWhatsNewM116DaysAfterFre;

// Key to store the number of launches after FRE.
extern NSString* const kWhatsNewLaunchesAfterFre;

// Key to store the number of launches after FRE for What's New M116.
extern NSString* const kWhatsNewM116LaunchesAfterFre;

// Key to store whether a user interacted with What's New from the overflow
// menu.
extern NSString* const kWhatsNewUsageEntryKey;

// Key to store whether a user interacted with What's New M116.
extern NSString* const kWhatsNewM116UsageEntryKey;

// Returns whether What's New was used in the overflow menu. This is used to
// decide on the location of the What's New entry point in the overflow menu.
bool WasWhatsNewUsed();

// Set that What's New was used in the overflow menu.
void SetWhatsNewUsed(PromosManager* promosManager);

// Set that What's New has been registered in the promo manager.
void setWhatsNewPromoRegistration();

// Returns whether What's New promo should be registered in the promo manager.
// This is used to avoid registering the What's New promo in the promo manager
// more than once.
bool ShouldRegisterWhatsNewPromo();

// Returns whether What's New M116 is enabled.
bool IsWhatsNewM116Enabled();

// Returns a string version of WhatsNewType.
const char* WhatsNewTypeToString(WhatsNewType type);

// Returns a string version of WhatsNewType only for M116 content.
const char* WhatsNewTypeToStringM116(WhatsNewType type);

// Vivaldi: We don't want to use and mess up with the chromium logic to show
// the first run or what's new related promo when browser is updated.
extern NSString* vWhatsNewWasShownKey;

bool IsVivaldiWhatsNewShown();
void setVivaldiWhatsNewShown(bool shown);
// End Vivaldi

#endif  // IOS_CHROME_BROWSER_UI_WHATS_NEW_WHATS_NEW_UTIL_H_
