// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import org.chromium.base.Callback;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.browser_ui.widget.selectable_list.SelectionDelegate;
import org.chromium.ui.modelutil.PropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableBooleanPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableIntPropertyKey;
import org.chromium.ui.modelutil.PropertyModel.WritableObjectPropertyKey;

import java.util.function.Function;

/**
 * Responsible for hosting properties of BookmarkToolbar views.
 * TODO(https://crbug.com/1435024) Remove all skipEquality=true usage.
 */
class BookmarkToolbarProperties {
    /** Dependencies */
    static final WritableObjectPropertyKey<BookmarkModel> BOOKMARK_MODEL =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<BookmarkOpener> BOOKMARK_OPENER =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<SelectionDelegate> SELECTION_DELEGATE =
            new WritableObjectPropertyKey<>();

    /** UI state properties. */
    // SelectableListToolbar calls #setTitle and we need to override that.
    static final WritableObjectPropertyKey<String> TITLE =
            new WritableObjectPropertyKey<>(/*skipEquality=*/true);
    static final WritableObjectPropertyKey<Integer> BOOKMARK_UI_MODE =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Boolean> SOFT_KEYBOARD_VISIBLE =
            new WritableObjectPropertyKey<>(/*skipEquality=*/true);
    static final WritableBooleanPropertyKey IS_DIALOG_UI = new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey DRAG_ENABLED = new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey SEARCH_BUTTON_VISIBLE =
            new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey EDIT_BUTTON_VISIBLE = new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey NEW_FOLDER_BUTTON_VISIBLE =
            new WritableBooleanPropertyKey();
    // Can change within SelectableListToolbar which makes the model value to become stale.
    static final WritableObjectPropertyKey<Integer> NAVIGATION_BUTTON_STATE =
            new WritableObjectPropertyKey<>(/*skipEquality=*/true);
    static final WritableIntPropertyKey CHECKED_SORT_MENU_ID = new WritableIntPropertyKey();
    static final WritableIntPropertyKey CHECKED_VIEW_MENU_ID = new WritableIntPropertyKey();

    static final WritableObjectPropertyKey<Boolean> FAKE_SELECTION_STATE_CHANGE =
            new WritableObjectPropertyKey<>(/*skipEquality=*/true);

    /** Bookmark state properties. */
    static final WritableObjectPropertyKey<BookmarkId> CURRENT_FOLDER =
            new WritableObjectPropertyKey<>(/*skipEquality=*/true);

    /** Callables to delegate business logic back to the mediator */
    static final WritableObjectPropertyKey<Function<Integer, Boolean>> MENU_ID_CLICKED_FUNCTION =
            new WritableObjectPropertyKey<>();
    static final WritableObjectPropertyKey<Callback<BookmarkId>> OPEN_FOLDER_CALLBACK =
            new WritableObjectPropertyKey<>();

    // Vivaldi
    static final WritableBooleanPropertyKey SORT_BUTTON_VISIBLE = new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey CLOSE_BUTTON_VISIBLE = new WritableBooleanPropertyKey();
    static final WritableBooleanPropertyKey ADD_TO_READING_LIST_BUTTON_VISIBLE =
            new WritableBooleanPropertyKey();

    static final PropertyKey[] ALL_KEYS = {BOOKMARK_MODEL, BOOKMARK_OPENER, SELECTION_DELEGATE,
            TITLE, BOOKMARK_UI_MODE, SOFT_KEYBOARD_VISIBLE, IS_DIALOG_UI, DRAG_ENABLED,
            SEARCH_BUTTON_VISIBLE, EDIT_BUTTON_VISIBLE, NEW_FOLDER_BUTTON_VISIBLE,
            NAVIGATION_BUTTON_STATE, CURRENT_FOLDER, CHECKED_SORT_MENU_ID, CHECKED_VIEW_MENU_ID,
            MENU_ID_CLICKED_FUNCTION, OPEN_FOLDER_CALLBACK, FAKE_SELECTION_STATE_CHANGE,
            // Vivaldi
            SORT_BUTTON_VISIBLE, CLOSE_BUTTON_VISIBLE, ADD_TO_READING_LIST_BUTTON_VISIBLE};
}
