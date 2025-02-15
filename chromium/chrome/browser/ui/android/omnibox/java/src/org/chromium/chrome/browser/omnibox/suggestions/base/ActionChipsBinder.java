// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions.base;

import android.view.View;
import android.view.ViewGroup;

import androidx.annotation.NonNull;

import org.chromium.chrome.browser.omnibox.R;
import org.chromium.chrome.browser.omnibox.suggestions.SuggestionCommonProperties;
import org.chromium.chrome.browser.ui.theme.BrandedColorScheme;
import org.chromium.components.browser_ui.widget.chips.ChipView;
import org.chromium.components.browser_ui.widget.chips.ChipViewBinder;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel;

/**
 * Binds ActionChipsView properties.
 */
public final class ActionChipsBinder {
    public static void bind(PropertyModel model, ActionChipsView view, PropertyKey propertyKey) {
        if (ActionChipsProperties.ACTION_CHIPS == propertyKey) {
            var isIncognito = model.get(SuggestionCommonProperties.COLOR_SCHEME)
                    == BrandedColorScheme.INCOGNITO;
            var chipList = model.get(ActionChipsProperties.ACTION_CHIPS);
            ActionChipsAdapter adapter = null;
            int actionChipsVisibility = View.GONE;

            if (chipList != null) {
                adapter = new ActionChipsAdapter(chipList);
                adapter.registerType(ActionChipsProperties.ViewType.CHIP,
                        parent -> createChipView(parent, isIncognito), ChipViewBinder::bind);
                actionChipsVisibility = View.VISIBLE;
            }
            view.setAdapter(adapter);
            view.setVisibility(actionChipsVisibility);
        }
    }

    public static ChipView createChipView(@NonNull ViewGroup parent, boolean isIncognito) {
        return new ChipView(parent.getContext(),
                isIncognito ? R.style.OmniboxIncognitoActionChipThemeOverlay
                            : R.style.OmniboxActionChipThemeOverlay);
    }
}
