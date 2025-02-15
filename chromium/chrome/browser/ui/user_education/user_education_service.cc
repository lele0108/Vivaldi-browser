// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/user_education/user_education_service.h"

#include <memory>

#include "components/user_education/common/feature_promo_registry.h"
#include "components/user_education/common/help_bubble_factory_registry.h"
#include "components/user_education/common/tutorial_registry.h"

const char kSidePanelCustomizeChromeTutorialId[] =
    "Side Panel Customize Chrome Tutorial";
const char kTabGroupTutorialId[] = "Tab Group Tutorial";
const char kTabGroupWithExistingGroupTutorialId[] =
    "Tab Group With Existing Group Tutorial";
const char kPasswordManagerTutorialId[] = "Password Manager Tutorial";

UserEducationService::UserEducationService()
    : tutorial_service_(&tutorial_registry_, &help_bubble_factory_registry_) {}

UserEducationService::~UserEducationService() = default;
