// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/flags/android/chrome_feature_list.h"

#include <stddef.h>

#include <string>

#include "base/android/feature_map.h"
#include "base/feature_list.h"
#include "base/no_destructor.h"
#include "chrome/browser/browser_features.h"
#include "chrome/browser/feature_guide/notifications/feature_notification_guide_service.h"
#include "chrome/browser/flags/android/chrome_session_state.h"
#include "chrome/browser/flags/jni_headers/ChromeFeatureMap_jni.h"
#include "chrome/browser/media/router/media_router_feature.h"
#include "chrome/browser/notifications/chime/android/features.h"
#include "chrome/browser/push_messaging/push_messaging_features.h"
#include "chrome/browser/share/share_features.h"
#include "chrome/browser/signin/signin_features.h"
#include "chrome/browser/thumbnail/cc/features.h"
#include "chrome/browser/ui/ui_features.h"
#include "chrome/common/chrome_features.h"
#include "components/autofill/core/common/autofill_features.h"
#include "components/autofill/core/common/autofill_payments_features.h"
#include "components/browser_sync/browser_sync_switches.h"
#include "components/browser_ui/photo_picker/android/features.h"
#include "components/commerce/core/commerce_feature_list.h"
#include "components/content_creation/notes/core/note_features.h"
#include "components/content_settings/core/common/features.h"
#include "components/contextual_search/core/browser/contextual_search_field_trial.h"
#include "components/download/public/common/download_features.h"
#include "components/embedder_support/android/util/cdn_utils.h"
#include "components/feature_engagement/public/feature_list.h"
#include "components/feed/feed_feature_list.h"
#include "components/history/core/browser/features.h"
#include "components/history_clusters/core/features.h"
#include "components/invalidation/impl/invalidation_switches.h"
#include "components/language/core/common/language_experiments.h"
#include "components/messages/android/messages_feature.h"
#include "components/ntp_tiles/features.h"
#include "components/offline_pages/core/offline_page_feature.h"
#include "components/omnibox/common/omnibox_features.h"
#include "components/optimization_guide/core/optimization_guide_features.h"
#include "components/page_info/core/features.h"
#include "components/paint_preview/features/features.h"
#include "components/password_manager/core/common/password_manager_features.h"
#include "components/permissions/features.h"
#include "components/policy/core/common/features.h"
#include "components/privacy_sandbox/privacy_sandbox_features.h"
#include "components/query_tiles/switches.h"
#include "components/reading_list/features/reading_list_switches.h"
#include "components/safe_browsing/core/common/features.h"
#include "components/segmentation_platform/public/features.h"
#include "components/send_tab_to_self/features.h"
#include "components/shared_highlighting/core/common/shared_highlighting_features.h"
#include "components/signin/public/base/signin_switches.h"
#include "components/subresource_filter/core/browser/subresource_filter_features.h"
#include "components/supervised_user/core/common/features.h"
#include "components/sync/base/features.h"
#include "components/webapps/browser/features.h"
#include "content/public/common/content_features.h"
#include "device/fido/features.h"
#include "media/base/media_switches.h"
#include "services/device/public/cpp/device_features.h"
#include "services/network/public/cpp/features.h"
#include "third_party/blink/public/common/features.h"
#include "ui/base/ui_base_features.h"

namespace chrome {
namespace android {

namespace {

// Array of features exposed through the Java ChromeFeatureList API. Entries in
// this array may either refer to features defined in the header of this file or
// in other locations in the code base (e.g. chrome/, components/, etc).
const base::Feature* const kFeaturesExposedToJava[] = {
    &autofill::features::kAutofillAccountProfileStorage,
    &autofill::features::kAutofillAddressProfileSavePromptNicknameSupport,
    &autofill::features::kAutofillEnableRankingFormulaAddressProfiles,
    &autofill::features::kAutofillEnableRankingFormulaCreditCards,
    &autofill::features::kAutofillEnableManualFallbackForVirtualCards,
    &autofill::features::kAutofillKeyboardAccessory,
    &autofill::features::kAutofillManualFallbackAndroid,
    &autofill::features::kAutofillEnableNewCardArtAndNetworkImages,
    &autofill::features::kAutofillEnableCardArtServerSideStretching,
    &autofill::features::kAutofillEnableSupportForHonorificPrefixes,
    &autofill::features::kAutofillEnableUpdateVirtualCardEnrollment,
    &autofill::features::kAutofillEnableVirtualCardMetadata,
    &autofill::features::kAutofillEnableCardArtImage,
    &autofill::features::kAutofillEnableCardProductName,
    &autofill::features::kAutofillTouchToFillForCreditCardsAndroid,
    &autofill::features::kAutofillEnablePaymentsMandatoryReauth,
    &blink::features::kForceWebContentsDarkMode,
    &blink::features::kPrerender2,
    &commerce::kCommerceMerchantViewer,
    &commerce::kCommercePriceTracking,
    &commerce::kShoppingList,
    &commerce::kShoppingPDPMetrics,
    &content_settings::kDarkenWebsitesCheckboxInThemesSetting,
    &download::features::kSmartSuggestionForLargeDownloads,
    &download::features::kUseDownloadOfflineContentProvider,
    &features::kPWAsDefaultOfflinePage,
    &features::kEarlyLibraryLoad,
    &features::kGenericSensorExtraClasses,
    &features::kBackForwardCache,
    &features::kBackForwardTransitions,
    &features::kHttpsOnlyMode,
    &features::kMetricsSettingsAndroid,
    &features::kNetworkServiceInProcess,
    &shared_highlighting::kPreemptiveLinkToTextGeneration,
    &shared_highlighting::kSharedHighlightingAmp,
    &features::kElasticOverscroll,
    &features::kPrivacyGuideAndroid,
    &features::kPrivacyGuideAndroidPostMVP,
    &features::kPushMessagingDisallowSenderIDs,
    &features::kPwaUpdateDialogForIcon,
    &features::kQuietNotificationPrompts,
    &features::kToolbarUseHardwareBitmapDraw,
    &features::kWebNfc,
    &features::kIncognitoDownloadsWarning,
    &features::kIncognitoNtpRevamp,
    &feature_engagement::kIPHNewTabPageHomeButtonFeature,
    &feature_engagement::kIPHTabSwitcherButtonFeature,
    &feature_engagement::kUseClientConfigIPH,
    &feature_guide::features::kFeatureNotificationGuide,
    &feature_guide::features::kSkipCheckForLowEngagedUsers,
    &feed::kCormorant,
    &feed::kFeedBackToTop,
    &feed::kFeedDynamicColors,
    &feed::kFeedFollowUiUpdate,
    &feed::kFeedHeaderStickToTop,
    &feed::kFeedImageMemoryCacheSizePercentage,
    &feed::kFeedLoadingPlaceholder,
    &feed::kFeedNoViewCache,
    &feed::kFeedPerformanceStudy,
    &feed::kFeedShowSignInCommand,
    &feed::kFeedSignedOutViewDemotion,
    &feed::kFeedBoCSigninInterstitial,
    &feed::kFeedUserInteractionReliabilityReport,
    &feed::kInterestFeedContentSuggestions,
    &feed::kInterestFeedV2,
    &feed::kInterestFeedV2Autoplay,
    &feed::kInterestFeedV2Hearts,
    &feed::kShareCrowButton,
    &feed::kWebFeed,
    &feed::kWebFeedAwareness,
    &feed::kWebFeedOnboarding,
    &feed::kWebFeedSort,
    &feed::kXsurfaceMetricsReporting,
    &history::kOrganicRepeatableQueries,
    &history_clusters::internal::kJourneys,
    &history_clusters::internal::kOmniboxAction,
    &history_clusters::internal::kOmniboxHistoryClusterProvider,
    &kAdaptiveButtonInTopToolbar,
    &kAdaptiveButtonInTopToolbarTranslate,
    &kAdaptiveButtonInTopToolbarAddToBookmarks,
    &kAdaptiveButtonInTopToolbarCustomizationV2,
    &kAddToHomescreenIPH,
    &kAllowNewIncognitoTabIntents,
    &kAndroidAppIntegration,
    &kAndroidAppIntegrationSafeSearch,
    &kAndroidSearchEngineChoiceNotification,
    &kAndroidImprovedBookmarks,
    &kAnimatedImageDragShadow,
    &kAppMenuMobileSiteOption,
    &kBackGestureActivityTabProvider,
    &kBackGestureRefactorActivityAndroid,
    &kBackGestureRefactorAndroid,
    &kBackgroundThreadPool,
    &kBaselineGM3SurfaceColors,
    &kCastDeviceFilter,
    &kClearOmniboxFocusAfterNavigation,
    &kCloseTabSuggestions,
    &kCloseTabSaveTabList,
    &kCriticalPersistedTabData,
    &kCreateNewTabInitializeRenderer,
    &kCCTBackgroundTab,
    &kCCTBottomBarSwipeUpGesture,
    &kCCTBrandTransparency,
    &kCCTClientDataHeader,
    &kCCTFeatureUsage,
    &kCCTIncognito,
    &kCCTIncognitoAvailableToThirdParty,
    &kCCTIntentFeatureOverrides,
    &kCCTNewDownloadTab,
    &kCCTPageInsightsHub,
    &kCCTPostMessageAPI,
    &kCCTPrefetchDelayShowOnStart,
    &kCCTRealTimeEngagementSignals,
    &kCCTRealTimeEngagementSignalsAlternativeImpl,
    &kCCTRedirectPreconnect,
    &kCCTRemoveRemoteViewIds,
    &kCCTReportParallelRequestStatus,
    &kCCTResizable90MaximumHeight,
    &kCCTResizableForThirdParties,
    &kCCTResizableSideSheet,
    &kCCTResizableSideSheetDiscoverFeedSettings,
    &kCCTResizableSideSheetForThirdParties,
    &kCCTRetainingStateInMemory,
    &kCCTResourcePrefetch,
    &kCCTTextFragmentLookupApiEnabled,
    &kCCTToolbarCustomizations,
    &kDontAutoHideBrowserControls,
    &kCacheDeprecatedSystemLocationSetting,
    &kChromeNewDownloadTab,
    &kChromeSharingHub,
    &kChromeSurveyNextAndroid,
    &kCommandLineOnNonRooted,
    &kContextMenuEnableLensShoppingAllowlist,
    &kContextMenuGoogleLensChip,
    &kContextMenuGoogleLensSearchOptimizations,
    &kContextMenuSearchWithGoogleLens,
    &kContextMenuShopWithGoogleLens,
    &kContextMenuSearchAndShopWithGoogleLens,
    &kContextMenuTranslateWithGoogleLens,
    &kContextMenuPopupForAllScreenSizes,
    &kContextualSearchDelayedIntelligence,
    &kContextualSearchDisableOnlineDetection,
    &kContextualSearchForceCaption,
    &kContextualSearchSuppressShortView,
    &kContextualSearchThinWebViewImplementation,
    &kDeferKeepScreenOnDuringGesture,
    &kDeferNotifyInMotion,
    &kDelayTempStripRemoval,
    &kDelayTransitionsForAnimation,
    &kDrawEdgeToEdge,
    &kEmptyStates,
    &kExperimentsForAgsa,
    &kExploreSites,
    &kFocusOmniboxInIncognitoTabIntents,
    &kFoldableJankFix,
    &kGridTabSwitcherForTablets,
    &kImprovedIncognitoScreenshot,
    &kIncognitoReauthenticationForAndroid,
    &kIncognitoScreenshot,
    &kInfobarScrollOptimization,
    &kInstanceSwitcher,
    &kInstantStart,
    &kLensCameraAssistedSearch,
    &kLensOnQuickActionSearchWidget,
    &kNotificationPermissionVariant,
    &kNotificationPermissionBottomSheet,
    &kPageAnnotationsService,
    &kPreconnectOnTabCreation,
    &kBookmarksImprovedSaveFlow,
    &kBookmarksRefresh,
    &kOmahaMinSdkVersionAndroid,
    &kOmniboxAdaptiveSuggestionsVisibleGroupEligibilityUpdate,
    &kOmniboxAdaptNarrowTabletWindows,
    &kOmniboxCacheSuggestionResources,
    &kOmniboxConsumesImeInsets,
    &kOmniboxWarmRecycledViewPool,
    &kOpaqueOriginForIncomingIntents,
    &kPartnerCustomizationsUma,
    &kProbabilisticCryptidRenderer,
    &kQuickDeleteForAndroid,
    &kReachedCodeProfiler,
    &kReaderModeInCCT,
    &kRecordSuppressionMetrics,
    &kReduceToolbarUpdatesForSameDocNavigations,
    &kReengagementNotification,
    &kRelatedSearches,
    &kReportParentalControlSitesChild,
    &kRequestDesktopSiteDefaults,
    &kRequestDesktopSiteDefaultsControl,
    &kRequestDesktopSiteDefaultsControlCohort1,
    &kRequestDesktopSiteDefaultsControlCohort2,
    &kRequestDesktopSiteDefaultsControlCohort3,
    &kRequestDesktopSiteDefaultsControlCohort4,
    &kRequestDesktopSiteDefaultsControlSynthetic,
    &kRequestDesktopSiteDefaultsEnabledCohort1,
    &kRequestDesktopSiteDefaultsEnabledCohort2,
    &kRequestDesktopSiteDefaultsEnabledCohort3,
    &kRequestDesktopSiteDefaultsEnabledCohort4,
    &kRequestDesktopSiteDefaultsSynthetic,
    &kRequestDesktopSiteOptInControlSynthetic,
    &kRequestDesktopSiteOptInSynthetic,
    &kRequestDesktopSiteDefaultsDowngrade,
    &kRequestDesktopSiteDefaultsLogging,
    &kRequestDesktopSitePerSiteIph,
    &kRestoreTabsOnFRE,
    &kSafeModeForCachedFlags,
    &kShowScrollableMVTOnNTPAndroid,
    &kFeedPositionAndroid,
    &kSearchResumptionModuleAndroid,
    &kScrollToTLDOptimization,
    &kShareSheetMigrationAndroid,
    &kShareSheetCustomActionsPolish,
    &kShouldIgnoreIntentSkipInternalCheck,
    &kSpecialLocaleWrapper,
    &kSpecialUserDecision,
    &kSuppressToolbarCaptures,
    &kSplitCompositorTask,
    &kStoreHoursAndroid,
    &kSwapPixelFormatToFixConvertFromTranslucent,
    &kTabDragDropAndroid,
    &kTabEngagementReportingAndroid,
    &kTabGroupsAndroid,
    &kTabGroupsContinuationAndroid,
    &kTabGroupsForTablets,
    &kDiscoverFeedMultiColumn,
    &kTabStripRedesign,
    &kTabletToolbarReordering,
    &kTabStripStartupRefactoring,
    &kTabGridLayoutAndroid,
    &kTabToGTSAnimation,
    &kTestDefaultDisabled,
    &kTestDefaultEnabled,
    &kThumbnailPlaceholder,
    &kToolbarMicIphAndroid,
    &kToolbarScrollAblationAndroid,
    &kTrustedWebActivityPostMessage,
    &kResizeOnlyActiveTab,
    &kSpareTab,
    &kStartSurfaceAndroid,
    &kStartSurfaceOnTablet,
    &kStartSurfaceReturnTime,
    &kStartSurfaceRefactor,
    &kStartSurfaceSpareTab,
    &kStartSurfaceDisabledFeedImprovement,
    &kStartSurfaceWithAccessibility,
    &kSurfacePolish,
    &kUmaBackgroundSessions,
    &kUpdateNotificationScheduleServiceImmediateShowOption,
    &kUseLibunwindstackNativeUnwinderAndroid,
    &kVoiceSearchAudioCapturePolicy,
    &kVoiceButtonInTopToolbar,
    &kWebOtpCrossDeviceSimpleString,
    &content_creation::kWebNotesStylizeEnabled,
    &kWebApkAllowIconUpdate,
    &kWebApkInstallService,
    &kWebApkTrampolineOnInitialIntent,
    &features::kDnsOverHttps,
    &notifications::features::kUseChimeAndroidSdk,
    &paint_preview::kPaintPreviewDemo,
    &language::kAppLanguagePrompt,
    &language::kAppLanguagePromptULP,
    &language::kCctAutoTranslate,
    &language::kDetailedLanguageSettings,
    &language::kExplicitLanguageAsk,
    &language::kForceAppLanguagePrompt,
    &language::kTranslateAssistContent,
    &language::kTranslateIntent,
    &media_router::kCafMRPDeferredDiscovery,
    &media_router::kCastAnotherContentWhileCasting,
    &messages::kMessagesForAndroidInfrastructure,
    &messages::kMessagesForAndroidSaveCard,
    &offline_pages::kOfflinePagesCTFeature,  // See crbug.com/620421.
    &offline_pages::kOfflinePagesDescriptiveFailStatusFeature,
    &offline_pages::kOfflinePagesDescriptivePendingStatusFeature,
    &offline_pages::kOfflinePagesLivePageSharingFeature,
    &omnibox::kOmniboxMatchToolbarAndStatusBarColor,
    &omnibox::kOmniboxModernizeVisualUpdate,
    &omnibox::kOmniboxMostVisitedTilesAddRecycledViewPool,
    &omnibox::kOmniboxOnClobberFocusTypeOnContent,
    &omnibox::kSuggestionAnswersColorReverse,
    &omnibox::kUpdatedConnectionSecurityIndicators,
    &optimization_guide::features::kPushNotifications,
    &page_info::kPageInfoAboutThisSiteImprovedBottomSheet,
    &page_info::kPageInfoAboutThisSiteMoreLangs,
    &page_info::kPageInfoAboutThisSiteNewIcon,
    &password_manager::features::kBiometricTouchToFill,
    &password_manager::features::kEnablePasswordsAccountStorage,
    &password_manager::features::kPasskeyManagementUsingAccountSettingsAndroid,
    &password_manager::features::kPasswordEditDialogWithDetails,
    &password_manager::features::kRecoverFromNeverSaveAndroid,
    &password_manager::features::kUnifiedCredentialManagerDryRun,
    &password_manager::features::kUnifiedPasswordManagerAndroid,
    &password_manager::features::kUnifiedPasswordManagerAndroidBranding,
    &password_manager::features::
        kUnifiedPasswordManagerLocalPasswordsMigrationWarning,
    &permissions::features::kBlockMidiByDefault,
    &privacy_sandbox::kPrivacySandboxFirstPartySetsUI,
    &privacy_sandbox::kPrivacySandboxSettings3,
    &privacy_sandbox::kPrivacySandboxSettings4,
    &query_tiles::features::kQueryTiles,
    &query_tiles::features::kQueryTilesInNTP,
    &query_tiles::features::kQueryTilesOnStart,
    &query_tiles::features::kQueryTilesSegmentation,
    &segmentation_platform::features::kContextualPageActions,
    &segmentation_platform::features::kContextualPageActionPriceTracking,
    &segmentation_platform::features::kContextualPageActionReaderMode,
    &segmentation_platform::features::kContextualPageActionShareModel,
    &send_tab_to_self::kSendTabToSelfSigninPromo,
    &send_tab_to_self::kSendTabToSelfV2,
    &share::kScreenshotsForAndroidV2,
    &supervised_user::kLocalWebApprovals,
    &supervised_user::kSynchronousSignInChecking,
    &supervised_user::kWebFilterInterstitialRefresh,
    &switches::kForceStartupSigninPromo,
    &switches::kIdentityStatusConsistency,
    &switches::kForceDisableExtendedSyncPromos,
    &switches::kTangibleSync,
    &syncer::kSyncAndroidLimitNTPPromoImpressions,
    &syncer::kSyncEnableContactInfoDataType,
    &syncer::kSyncEnableContactInfoDataTypeInTransportMode,
    &syncer::kSyncEnableHistoryDataType,
    &subresource_filter::kSafeBrowsingSubresourceFilter,
    &thumbnail::kThumbnailCacheRefactor,
    &webapps::features::kInstallableAmbientBadgeInfoBar,
    &webapps::features::kInstallableAmbientBadgeMessage,
    &webapps::features::kWebApkInstallFailureNotification,
    &webapps::features::kWebApkInstallFailureRetry,
    &webapps::features::kAmbientBadgeSuppressFirstVisit,
    &network::features::kPrivateStateTokens,
};

// static
base::android::FeatureMap* GetFeatureMap() {
  static base::NoDestructor<base::android::FeatureMap> kFeatureMap(std::vector(
      std::begin(kFeaturesExposedToJava), std::end(kFeaturesExposedToJava)));
  return kFeatureMap.get();
}

}  // namespace

static jlong JNI_ChromeFeatureMap_GetNativeMap(JNIEnv* env) {
  return reinterpret_cast<jlong>(GetFeatureMap());
}

// Alphabetical:

BASE_FEATURE(kAdaptiveButtonInTopToolbar,
             "AdaptiveButtonInTopToolbar",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAdaptiveButtonInTopToolbarTranslate,
             "AdaptiveButtonInTopToolbarTranslate",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAdaptiveButtonInTopToolbarAddToBookmarks,
             "AdaptiveButtonInTopToolbarAddToBookmarks",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAdaptiveButtonInTopToolbarCustomizationV2,
             "AdaptiveButtonInTopToolbarCustomizationV2",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kAddToHomescreenIPH,
             "AddToHomescreenIPH",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAllowNewIncognitoTabIntents,
             "AllowNewIncognitoTabIntents",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kFocusOmniboxInIncognitoTabIntents,
             "FocusOmniboxInIncognitoTabIntents",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kAndroidAppIntegration,
             "AndroidAppIntegration",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAndroidAppIntegrationSafeSearch,
             "AndroidAppIntegrationSafeSearch",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAndroidSearchEngineChoiceNotification,
             "AndroidSearchEngineChoiceNotification",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kAndroidImprovedBookmarks,
             "AndroidImprovedBookmarks",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAnimatedImageDragShadow,
             "AnimatedImageDragShadow",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kAppMenuMobileSiteOption,
             "AppMenuMobileSiteOption",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBackgroundThreadPool,
             "BackgroundThreadPool",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBaselineGM3SurfaceColors,
             "BaselineGM3SurfaceColors",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Used in downstream code.
BASE_FEATURE(kCastDeviceFilter,
             "CastDeviceFilter",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kClearOmniboxFocusAfterNavigation,
             "ClearOmniboxFocusAfterNavigation",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCloseTabSuggestions,
             "CloseTabSuggestions",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCloseTabSaveTabList,
             "CloseTabSaveTabList",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCreateNewTabInitializeRenderer,
             "CreateNewTabInitializeRenderer",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCriticalPersistedTabData,
             "CriticalPersistedTabData",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTBottomBarSwipeUpGesture,
             "CCTBottomBarSwipeUpGesture",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTBackgroundTab,
             "CCTBackgroundTab",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTBrandTransparency,
             "CCTBrandTransparency",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTClientDataHeader,
             "CCTClientDataHeader",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTNewDownloadTab,
             "CCTNewDownloadTab",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTFeatureUsage,
             "CCTFeatureUsage",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTIncognito, "CCTIncognito", base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTIncognitoAvailableToThirdParty,
             "CCTIncognitoAvailableToThirdParty",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTIntentFeatureOverrides,
             "CCTIntentFeatureOverrides",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTPageInsightsHub,
             "CCTPageInsightsHub",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTPostMessageAPI,
             "CCTPostMessageAPI",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTPrefetchDelayShowOnStart,
             "CCTPrefetchDelayShowOnStart",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTRealTimeEngagementSignals,
             "CCTRealTimeEngagementSignals",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTRealTimeEngagementSignalsAlternativeImpl,
             "CCTRealTimeEngagementSignalsAlternativeImpl",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTRedirectPreconnect,
             "CCTRedirectPreconnect",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTRemoveRemoteViewIds,
             "CCTRemoveRemoteViewIds",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTReportParallelRequestStatus,
             "CCTReportParallelRequestStatus",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResizable90MaximumHeight,
             "CCTResizable90MaximumHeight",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResizableForThirdParties,
             "CCTResizableForThirdParties",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResizableSideSheet,
             "CCTResizableSideSheet",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResizableSideSheetDiscoverFeedSettings,
             "CCTResizableSideSheetDiscoverFeedSettings",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResizableSideSheetForThirdParties,
             "CCTResizableSideSheetForThirdParties",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTResourcePrefetch,
             "CCTResourcePrefetch",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTRetainingStateInMemory,
             "CCTRetainingStateInMemory",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCCTTextFragmentLookupApiEnabled,
             "CCTTextFragmentLookupApiEnabled",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCCTToolbarCustomizations,
             "CCTToolbarCustomizations",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDontAutoHideBrowserControls,
             "DontAutoHideBrowserControls",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kCacheDeprecatedSystemLocationSetting,
             "CacheDeprecatedSystemLocationSetting",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kChromeNewDownloadTab,
             "ChromeNewDownloadTab",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kChromeSharingHub,
             "ChromeSharingHub",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kChromeSurveyNextAndroid,
             "ChromeSurveyNextAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kCommandLineOnNonRooted,
             "CommandLineOnNonRooted",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuEnableLensShoppingAllowlist,
             "ContextMenuEnableLensShoppingAllowlist",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuGoogleLensChip,
             "ContextMenuGoogleLensChip",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuPopupForAllScreenSizes,
             "ContextMenuPopupForAllScreenSizes",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuGoogleLensSearchOptimizations,
             "ContextMenuGoogleLensSearchOptimizations",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuSearchWithGoogleLens,
             "ContextMenuSearchWithGoogleLens",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuShopWithGoogleLens,
             "ContextMenuShopWithGoogleLens",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuSearchAndShopWithGoogleLens,
             "ContextMenuSearchAndShopWithGoogleLens",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextMenuTranslateWithGoogleLens,
             "ContextMenuTranslateWithGoogleLens",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kLensCameraAssistedSearch,
             "LensCameraAssistedSearch",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kLensOnQuickActionSearchWidget,
             "LensOnQuickActionSearchWidget",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kContextualSearchDelayedIntelligence,
             "ContextualSearchDelayedIntelligence",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextualSearchDisableOnlineDetection,
             "ContextualSearchDisableOnlineDetection",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextualSearchForceCaption,
             "ContextualSearchForceCaption",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kContextualSearchSuppressShortView,
             "ContextualSearchSuppressShortView",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kContextualSearchThinWebViewImplementation,
             "ContextualSearchThinWebViewImplementation",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kDeferKeepScreenOnDuringGesture,
             "DeferKeepScreenOnDuringGesture",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDeferNotifyInMotion,
             "DeferNotifyInMotion",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kDelayTempStripRemoval,
             "DelayTempStripRemoval",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDelayTransitionsForAnimation,
             "DelayTransitionsForAnimation",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDownloadAutoResumptionThrottling,
             "DownloadAutoResumptionThrottling",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDownloadHomeForExternalApp,
             "DownloadHomeForExternalApp",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDrawEdgeToEdge,
             "DrawEdgeToEdge",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kEmptyStates, "EmptyStates", base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kExperimentsForAgsa,
             "ExperimentsForAgsa",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kExploreSites, "ExploreSites", base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kFoldableJankFix,
             "FoldableJankFix",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kGridTabSwitcherForTablets,
             "GridTabSwitcherForTablets",
             base::FEATURE_ENABLED_BY_DEFAULT);

// Only affects devices on API version >= 33.
BASE_FEATURE(kImprovedIncognitoScreenshot,
             "ImprovedIncognitoScreenshot",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kIncognitoReauthenticationForAndroid,
             "IncognitoReauthenticationForAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kIncognitoScreenshot,
             "IncognitoScreenshot",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kInfobarScrollOptimization,
             "InfobarScrollOptimization",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kInstantStart, "InstantStart", base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kNotificationPermissionVariant,
             "NotificationPermissionVariant",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kNotificationPermissionBottomSheet,
             "NotificationPermissionBottomSheet",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kInstanceSwitcher,
             "InstanceSwitcher",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPageAnnotationsService,
             "PageAnnotationsService",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kPreconnectOnTabCreation,
             "PreconnectOnTabCreation",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBookmarksImprovedSaveFlow,
             "BookmarksImprovedSaveFlow",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBookmarksRefresh,
             "BookmarksRefresh",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBackGestureActivityTabProvider,
             "BackGestureActivityTabProvider",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBackGestureRefactorActivityAndroid,
             "BackGestureRefactorActivityAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kBackGestureRefactorAndroid,
             "BackGestureRefactorAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOmahaMinSdkVersionAndroid,
             "OmahaMinSdkVersionAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, considers suggestions exposed 50% or more as fully visible and
// 49% or less as hidden for Adaptive Suggestions partial grouping.
BASE_FEATURE(kOmniboxAdaptiveSuggestionsVisibleGroupEligibilityUpdate,
             "AdaptiveSuggestionsVisibleGroupEligibilityUpdate",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOmniboxAdaptNarrowTabletWindows,
             "OmniboxAdaptNarrowTabletWindows",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kOmniboxCacheSuggestionResources,
             "OmniboxCacheSuggestionResources",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOmniboxConsumesImeInsets,
             "OmniboxConsumesImeInsets",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOmniboxWarmRecycledViewPool,
             "OmniboxWarmRecycledViewPool",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kOpaqueOriginForIncomingIntents,
             "OpaqueOriginForIncomingIntents",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kPartnerCustomizationsUma,
             "PartnerCustomizationsUma",
             base::FEATURE_ENABLED_BY_DEFAULT);
BASE_FEATURE(kProbabilisticCryptidRenderer,
             "ProbabilisticCryptidRenderer",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kQuickDeleteForAndroid,
             "QuickDeleteForAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kReachedCodeProfiler,
             "ReachedCodeProfiler",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kReaderModeInCCT,
             "ReaderModeInCCT",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kRecordSuppressionMetrics,
             "RecordSuppressionMetrics",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kReduceToolbarUpdatesForSameDocNavigations,
             "ReduceToolbarUpdatesForSameDocNavigations",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kReengagementNotification,
             "ReengagementNotification",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRelatedSearches,
             "RelatedSearches",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kReportParentalControlSitesChild,
             "ReportParentalControlSitesChild",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaults,
             "RequestDesktopSiteDefaults",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControl,
             "RequestDesktopSiteDefaultsControl",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControlCohort1,
             "RequestDesktopSiteDefaultsControlCohort1",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControlCohort2,
             "RequestDesktopSiteDefaultsControlCohort2",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControlCohort3,
             "RequestDesktopSiteDefaultsControlCohort3",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControlCohort4,
             "RequestDesktopSiteDefaultsControlCohort4",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsControlSynthetic,
             "RequestDesktopSiteDefaultsControlSynthetic",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsEnabledCohort1,
             "RequestDesktopSiteDefaultsEnabledCohort1",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsEnabledCohort2,
             "RequestDesktopSiteDefaultsEnabledCohort2",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsEnabledCohort3,
             "RequestDesktopSiteDefaultsEnabledCohort3",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsEnabledCohort4,
             "RequestDesktopSiteDefaultsEnabledCohort4",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsSynthetic,
             "RequestDesktopSiteDefaultsSynthetic",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteOptInControlSynthetic,
             "RequestDesktopSiteOptInControlSynthetic",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteOptInSynthetic,
             "RequestDesktopSiteOptInSynthetic",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsDowngrade,
             "RequestDesktopSiteDefaultsDowngrade",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSiteDefaultsLogging,
             "RequestDesktopSiteDefaultsLogging",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRequestDesktopSitePerSiteIph,
             "RequestDesktopSitePerSiteIph",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kRestoreTabsOnFRE,
             "RestoreTabsOnFRE",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSafeModeForCachedFlags,
             "SafeModeForCachedFlags",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kScrollToTLDOptimization,
             "ScrollToTLDOptimization",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kShowScrollableMVTOnNTPAndroid,
             "ShowScrollableMVTOnNTPAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kShareSheetCustomActionsPolish,
             "ShareSheetCustomActionsPolish",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kShareSheetMigrationAndroid,
             "ShareSheetMigrationAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kSpecialLocaleWrapper,
             "SpecialLocaleWrapper",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kSpecialUserDecision,
             "SpecialUserDecision",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSplitCompositorTask,
             "SplitCompositorTask",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kStoreHoursAndroid,
             "StoreHoursAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSuppressToolbarCaptures,
             "SuppressToolbarCaptures",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kSwapPixelFormatToFixConvertFromTranslucent,
             "SwapPixelFormatToFixConvertFromTranslucent",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabDragDropAndroid,
             "TabDragDropAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabEngagementReportingAndroid,
             "TabEngagementReportingAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enabled, but used in tests to simulate behaviors for configurations where tab
// groups are currently unsupported (low-end devices and users of the
// accessibility tab switcher).
BASE_FEATURE(kTabGroupsAndroid,
             "TabGroupsAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabGroupsContinuationAndroid,
             "TabGroupsContinuationAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabGroupsForTablets,
             "TabGroupsForTablets",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabGridLayoutAndroid,
             "TabGridLayoutAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kDiscoverFeedMultiColumn,
             "DiscoverFeedMultiColumn",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTabStripRedesign,
             "TabStripRedesign",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabletToolbarReordering,
             "TabletToolbarReordering",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTabStripStartupRefactoring,
             "TabStripStartupRefactoring",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enabled, but often disabled in tests to reduce animation flakes and test
// low-end device behavior where this animation is disabled.
BASE_FEATURE(kTabToGTSAnimation,
             "TabToGTSAnimation",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kTestDefaultDisabled,
             "TestDefaultDisabled",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTestDefaultEnabled,
             "TestDefaultEnabled",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kThumbnailPlaceholder,
             "ThumbnailPlaceholder",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kToolbarMicIphAndroid,
             "ToolbarMicIphAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kToolbarScrollAblationAndroid,
             "ToolbarScrollAblationAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kTrustedWebActivityPostMessage,
             "TrustedWebActivityPostMessage",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kResizeOnlyActiveTab,
             "ResizeOnlyActiveTab",
             base::FEATURE_ENABLED_BY_DEFAULT);

// SpareTab is enabled by default for configuring renderer initialization
// through field trial. Users of spareTab should declare their own field trial
// feature.
BASE_FEATURE(kSpareTab, "SpareTab", base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceAndroid,
             "StartSurfaceAndroid",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kFeedPositionAndroid,
             "FeedPositionAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSearchResumptionModuleAndroid,
             "SearchResumptionModuleAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kShouldIgnoreIntentSkipInternalCheck,
             "ShouldIgnoreIntentSkipInternalCheck",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceOnTablet,
             "StartSurfaceOnTablet",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceSpareTab,
             "StartSurfaceSpareTab",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceReturnTime,
             "StartSurfaceReturnTime",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceRefactor,
             "StartSurfaceRefactor",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceDisabledFeedImprovement,
             "StartSurfaceDisabledFeedImprovement",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kStartSurfaceWithAccessibility,
             "StartSurfaceWithAccessibility",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kSurfacePolish,
             "SurfacePolish",
             base::FEATURE_DISABLED_BY_DEFAULT);

// If enabled, keep logging and reporting UMA while chrome is backgrounded.
BASE_FEATURE(kUmaBackgroundSessions,
             "UMABackgroundSessions",
             base::FEATURE_ENABLED_BY_DEFAULT);

BASE_FEATURE(kUpdateNotificationScheduleServiceImmediateShowOption,
             "UpdateNotificationScheduleServiceImmediateShowOption",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Use the LibunwindstackNativeUnwinderAndroid for only browser main thread, and
// only on Android.
BASE_FEATURE(kUseLibunwindstackNativeUnwinderAndroid,
             "UseLibunwindstackNativeUnwinderAndroid",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kUserMediaScreenCapturing,
             "UserMediaScreenCapturing",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kVoiceSearchAudioCapturePolicy,
             "VoiceSearchAudioCapturePolicy",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kVoiceButtonInTopToolbar,
             "VoiceButtonInTopToolbar",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Shows only the remote device name on the Android notification instead of
// a descriptive text.
BASE_FEATURE(kWebOtpCrossDeviceSimpleString,
             "WebOtpCrossDeviceSimpleString",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kWebApkAllowIconUpdate,
             "WebApkAllowIconUpdate",
             base::FEATURE_DISABLED_BY_DEFAULT);

// Enables the Chrome Android WebAPK-install service.
BASE_FEATURE(kWebApkInstallService,
             "WebApkInstallService",
             base::FEATURE_DISABLED_BY_DEFAULT);

BASE_FEATURE(kWebApkTrampolineOnInitialIntent,
             "WebApkTrampolineOnInitialIntent",
             base::FEATURE_ENABLED_BY_DEFAULT);

}  // namespace android
}  // namespace chrome
