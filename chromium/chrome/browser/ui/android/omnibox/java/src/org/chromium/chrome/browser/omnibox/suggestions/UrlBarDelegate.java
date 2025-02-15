// Copyright 2020 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions;

/** An interface for modifying the location bar and its contents. */
public interface UrlBarDelegate {
    /** Remove focus from the omnibox. */
    void clearOmniboxFocus();

    /**
     * Set the text in the omnibox.
     * @param text The text that should be displayed in the omnibox.
     */
    void setOmniboxEditingText(String text);

    /**
     * Returns {@code true} when the omnibox should be cleared on focus, {@code false} otherwise.
     */
    default boolean shouldClearOmniboxOnFocus() {
        return true;
    }

    /**
     * Vivaldi
     * Set the text in the omnibox.
     * @param text The text that should be displayed in the omnibox.
     * @param isRefineSuggestion Whether a suggestion is selected to refine it.
     */
    default void setOmniboxEditingText(String text, boolean isRefineSuggestion) {
        setOmniboxEditingText(text);
    }
}
