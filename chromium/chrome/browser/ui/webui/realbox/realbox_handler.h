// Copyright 2021 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_WEBUI_REALBOX_REALBOX_HANDLER_H_
#define CHROME_BROWSER_UI_WEBUI_REALBOX_REALBOX_HANDLER_H_

#include <memory>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "base/scoped_observation.h"
#include "components/omnibox/browser/autocomplete_controller.h"
#include "components/omnibox/browser/location_bar_model.h"
#include "components/omnibox/browser/omnibox.mojom.h"
#include "components/url_formatter/spoof_checks/idna_metrics.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/remote.h"
#include "ui/gfx/vector_icon_types.h"

class GURL;
class MetricsReporter;
class OmniboxController;
class OmniboxEditModel;
class Profile;

namespace content {
class WebContents;
class WebUIDataSource;
}  // namespace content

// Handles bidirectional communication between NTP realbox JS and the browser.
class RealboxHandler : public omnibox::mojom::PageHandler,
                       public AutocompleteController::Observer,
                       public LocationBarModel {
 public:
  enum class FocusState {
    // kNormal means the row is focused, and Enter key navigates to the match.
    kFocusedMatch,

    // kFocusedButtonRemoveSuggestion state means the Remove Suggestion (X)
    // button is focused. Pressing enter will attempt to remove this suggestion.
    kFocusedButtonRemoveSuggestion,
  };

  static void SetupWebUIDataSource(content::WebUIDataSource* source,
                                   Profile* profile);
  static void SetupDropdownWebUIDataSource(content::WebUIDataSource* source,
                                           Profile* profile);
  static std::string AutocompleteMatchVectorIconToResourceName(
      const gfx::VectorIcon& icon);
  static std::string PedalVectorIconToResourceName(const gfx::VectorIcon& icon);

  RealboxHandler(
      mojo::PendingReceiver<omnibox::mojom::PageHandler> pending_page_handler,
      Profile* profile,
      content::WebContents* web_contents,
      MetricsReporter* metrics_reporter,
      bool is_omnibox_popup_handler);

  RealboxHandler(const RealboxHandler&) = delete;
  RealboxHandler& operator=(const RealboxHandler&) = delete;

  ~RealboxHandler() override;

  // omnibox::mojom::PageHandler:
  void SetPage(mojo::PendingRemote<omnibox::mojom::Page> pending_page) override;
  void OnFocusChanged(bool focused) override;
  void QueryAutocomplete(const std::u16string& input,
                         bool prevent_inline_autocomplete) override;
  void StopAutocomplete(bool clear_result) override;
  void OpenAutocompleteMatch(uint8_t line,
                             const GURL& url,
                             bool are_matches_showing,
                             uint8_t mouse_button,
                             bool alt_key,
                             bool ctrl_key,
                             bool meta_key,
                             bool shift_key) override;
  void DeleteAutocompleteMatch(uint8_t line, const GURL& url) override;
  void ToggleSuggestionGroupIdVisibility(int32_t suggestion_group_id) override;
  void ExecuteAction(uint8_t line,
                     uint8_t action_index,
                     const GURL& url,
                     base::TimeTicks match_selection_timestamp,
                     uint8_t mouse_button,
                     bool alt_key,
                     bool ctrl_key,
                     bool meta_key,
                     bool shift_key) override;
  void OnNavigationLikely(
      uint8_t line,
      const GURL& url,
      omnibox::mojom::NavigationPredictor navigation_predictor) override;

  // AutocompleteController::Observer:
  void OnResultChanged(AutocompleteController* controller,
                       bool default_match_changed) override;

  void SelectMatchAtLine(size_t old_line, size_t new_line);

  // LocationBarModel:
  std::u16string GetFormattedFullURL() const override;
  std::u16string GetURLForDisplay() const override;
  GURL GetURL() const override;
  security_state::SecurityLevel GetSecurityLevel() const override;
  net::CertStatus GetCertStatus() const override;
  metrics::OmniboxEventProto::PageClassification GetPageClassification(
      OmniboxFocusSource focus_source,
      bool is_prefetch = false) override;
  const gfx::VectorIcon& GetVectorIcon() const override;
  std::u16string GetSecureDisplayText() const override;
  std::u16string GetSecureAccessibilityText() const override;
  bool ShouldDisplayURL() const override;
  bool IsOfflinePage() const override;
  bool ShouldPreventElision() const override;
  bool ShouldUseUpdatedConnectionSecurityIndicators() const override;

 private:
  OmniboxEditModel* edit_model() const;
  AutocompleteController* autocomplete_controller() const;
  const AutocompleteMatch* GetMatchWithUrl(size_t index, const GURL& url);

  raw_ptr<Profile> profile_;
  raw_ptr<content::WebContents> web_contents_;
  raw_ptr<MetricsReporter> metrics_reporter_;
  raw_ptr<OmniboxController> controller_;
  std::unique_ptr<OmniboxController> owned_controller_;
  base::ScopedObservation<AutocompleteController,
                          AutocompleteController::Observer>
      autocomplete_controller_observation_{this};

  mojo::Remote<omnibox::mojom::Page> page_;
  mojo::Receiver<omnibox::mojom::PageHandler> page_handler_;

  // This is unused, it's just needed for LocationBarModel implementation.
  gfx::VectorIcon vector_icon_{nullptr, 0u, ""};

  base::WeakPtrFactory<RealboxHandler> weak_ptr_factory_{this};
};

#endif  // CHROME_BROWSER_UI_WEBUI_REALBOX_REALBOX_HANDLER_H_
