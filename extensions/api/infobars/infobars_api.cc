// Copyright (c) 2018 Vivaldi Technologies AS. All rights reserved

#include "extensions/api/infobars/infobars_api.h"

#include "chrome/browser/profiles/profile.h"
#include "components/infobars/content/content_infobar_manager.h"
#include "components/infobars/core/confirm_infobar_delegate.h"
#include "components/infobars/core/infobar.h"
#include "extensions/tools/vivaldi_tools.h"
#include "ui/vivaldi_ui_utils.h"
#include "vivaldi/extensions/schema/infobars.h"

namespace extensions {

ExtensionFunction::ResponseAction InfobarsSendButtonActionFunction::Run() {
  using vivaldi::infobars::SendButtonAction::Params;

  absl::optional<Params> params = Params::Create(args());
  EXTENSION_FUNCTION_VALIDATE(params);

  int identifier = params->identifier;
  int tab_id = params->tab_id;
  std::string action = params->action;

  std::string error;
  content::WebContents* contents =
      ::vivaldi::ui_tools::GetWebContentsFromTabStrip(tab_id, browser_context(),
                                                      &error);
  if (!contents)
    return RespondNow(Error(error));

  auto* service = infobars::ContentInfoBarManager::FromWebContents(contents);
  for (size_t i = 0; i < service->infobar_count(); i++) {
    infobars::InfoBar* infobar = service->infobar_at(i);
    if (infobar->delegate()->GetIdentifier() ==
        static_cast<infobars::InfoBarDelegate::InfoBarIdentifier>(identifier)) {
      ConfirmInfoBarDelegate* delegate =
          infobar->delegate()->AsConfirmInfoBarDelegate();
      DCHECK(delegate);
      if (delegate) {
        base::Value::List args(
            extensions::vivaldi::infobars::OnInfobarRemoved::Create(
                tab_id, delegate->GetIdentifier()));
        ::vivaldi::BroadcastEvent(
            extensions::vivaldi::infobars::OnInfobarRemoved::kEventName,
            std::move(args), browser_context());

        if (action == extensions::vivaldi::infobars::ToString(
                          extensions::vivaldi::infobars::ButtonAction::
                              BUTTON_ACTION_ACCEPT)) {
          delegate->Accept();
        } else if (action == extensions::vivaldi::infobars::ToString(
                                 extensions::vivaldi::infobars::ButtonAction::
                                     BUTTON_ACTION_CANCEL)) {
          delegate->Cancel();
        } else if (action == extensions::vivaldi::infobars::ToString(
                                 extensions::vivaldi::infobars::ButtonAction::
                                     BUTTON_ACTION_DISMISS)) {
          delegate->InfoBarDismissed();
        }
        infobar->RemoveSelf();
      }
    }
  }

  return RespondNow(NoArguments());
}

}  // namespace extensions
