// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import static org.mockito.Mockito.doReturn;

import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.bookmarks.BookmarkItem;
import org.chromium.components.bookmarks.BookmarkType;
import org.chromium.components.power_bookmarks.PowerBookmarkMeta;
import org.chromium.components.power_bookmarks.ShoppingSpecifics;
import org.chromium.url.GURL;

import java.util.Arrays;
import java.util.Collections;

/** Shared set of {@link BookmarkModel} mocks used by multiple tests. */
public class SharedBookmarkModelMocks {
    // Used to make sure all the bookmark ids are different.
    private static int sId;

    static final BookmarkId ROOT_BOOKMARK_ID = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId DESKTOP_BOOKMARK_ID = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId OTHER_BOOKMARK_ID = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId MOBILE_BOOKMARK_ID = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId READING_LIST_BOOKMARK_ID =
            new BookmarkId(sId++, BookmarkType.READING_LIST);

    static final BookmarkId FOLDER_BOOKMARK_ID_A = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId URL_BOOKMARK_ID_A = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId URL_BOOKMARK_ID_B = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId URL_BOOKMARK_ID_C = new BookmarkId(sId++, BookmarkType.NORMAL);
    static final BookmarkId URL_BOOKMARK_ID_D = new BookmarkId(sId++, BookmarkType.READING_LIST);
    static final BookmarkId URL_BOOKMARK_ID_E = new BookmarkId(sId++, BookmarkType.READING_LIST);

    static final GURL URL_A = new GURL("https://www.a.com/");
    static final GURL URL_B = new GURL("https://www.b.com/");
    static final GURL URL_C = new GURL("https://www.c.com/");
    static final GURL URL_D = new GURL("https://www.d.com/");
    static final GURL URL_E = new GURL("https://www.e.com/");

    static final BookmarkItem DESKTOP_BOOKMARK_ITEM =
            makeFolderItem(DESKTOP_BOOKMARK_ID, "Bookmarks bar", ROOT_BOOKMARK_ID);
    static final BookmarkItem OTHER_BOOKMARK_ITEM =
            makeFolderItem(OTHER_BOOKMARK_ID, "Other bookmarks", ROOT_BOOKMARK_ID);
    static final BookmarkItem MOBILE_BOOKMARK_ITEM =
            makeFolderItem(MOBILE_BOOKMARK_ID, "Mobile bookmarks", ROOT_BOOKMARK_ID);
    static final BookmarkItem READING_LIST_ITEM =
            makeFolderItem(READING_LIST_BOOKMARK_ID, "Reading list", ROOT_BOOKMARK_ID);
    static final BookmarkItem FOLDER_ITEM_A =
            makeFolderItem(FOLDER_BOOKMARK_ID_A, "Folder A", MOBILE_BOOKMARK_ID);
    static final BookmarkItem URL_ITEM_A =
            makeUrlItem(URL_BOOKMARK_ID_A, "Url A", URL_A, MOBILE_BOOKMARK_ID);
    static final BookmarkItem URL_ITEM_B =
            makeUrlItem(URL_BOOKMARK_ID_B, "Url B", URL_B, FOLDER_BOOKMARK_ID_A);
    static final BookmarkItem URL_ITEM_C =
            makeUrlItem(URL_BOOKMARK_ID_C, "Url C", URL_C, FOLDER_BOOKMARK_ID_A);
    static final BookmarkItem URL_ITEM_D =
            makeUrlItemWithRead(URL_BOOKMARK_ID_D, "Url D", URL_D, READING_LIST_BOOKMARK_ID, true);
    static final BookmarkItem URL_ITEM_E =
            makeUrlItem(URL_BOOKMARK_ID_E, "Url E", URL_E, READING_LIST_BOOKMARK_ID);

    public static void initMocks(BookmarkModel bookmarkModel) {
        doReturn(ROOT_BOOKMARK_ID).when(bookmarkModel).getRootFolderId();
        doReturn(DESKTOP_BOOKMARK_ID).when(bookmarkModel).getDesktopFolderId();
        doReturn(OTHER_BOOKMARK_ID).when(bookmarkModel).getOtherFolderId();
        doReturn(MOBILE_BOOKMARK_ID).when(bookmarkModel).getMobileFolderId();
        doReturn(READING_LIST_BOOKMARK_ID).when(bookmarkModel).getReadingListFolder();
        doReturn(Collections.singletonList(READING_LIST_BOOKMARK_ID))
                .when(bookmarkModel)
                .getTopLevelFolderIds(/*getSpecial*/ true, /*getNormal*/ false);

        doReturn(DESKTOP_BOOKMARK_ITEM).when(bookmarkModel).getBookmarkById(DESKTOP_BOOKMARK_ID);
        doReturn(OTHER_BOOKMARK_ITEM).when(bookmarkModel).getBookmarkById(OTHER_BOOKMARK_ID);
        doReturn(MOBILE_BOOKMARK_ITEM).when(bookmarkModel).getBookmarkById(MOBILE_BOOKMARK_ID);
        doReturn(READING_LIST_ITEM).when(bookmarkModel).getBookmarkById(READING_LIST_BOOKMARK_ID);
        doReturn(FOLDER_ITEM_A).when(bookmarkModel).getBookmarkById(FOLDER_BOOKMARK_ID_A);
        doReturn(URL_ITEM_A).when(bookmarkModel).getBookmarkById(URL_BOOKMARK_ID_A);
        doReturn(URL_ITEM_B).when(bookmarkModel).getBookmarkById(URL_BOOKMARK_ID_B);
        doReturn(URL_ITEM_C).when(bookmarkModel).getBookmarkById(URL_BOOKMARK_ID_C);
        doReturn(URL_ITEM_D).when(bookmarkModel).getBookmarkById(URL_BOOKMARK_ID_D);
        doReturn(URL_ITEM_E).when(bookmarkModel).getBookmarkById(URL_BOOKMARK_ID_E);

        doReturn(true).when(bookmarkModel).isFolderVisible(DESKTOP_BOOKMARK_ID);
        doReturn(false).when(bookmarkModel).isFolderVisible(OTHER_BOOKMARK_ID);
        doReturn(true).when(bookmarkModel).isFolderVisible(MOBILE_BOOKMARK_ID);
        doReturn(Arrays.asList(FOLDER_BOOKMARK_ID_A, URL_BOOKMARK_ID_A))
                .when(bookmarkModel)
                .getChildIds(MOBILE_BOOKMARK_ID);
        doReturn(Arrays.asList(URL_BOOKMARK_ID_B, URL_BOOKMARK_ID_C))
                .when(bookmarkModel)
                .getChildIds(BookmarkId.SHOPPING_FOLDER);
        ShoppingSpecifics shoppingSpecifics =
                ShoppingSpecifics.newBuilder().setIsPriceTracked(true).build();
        PowerBookmarkMeta powerBookmarkMeta =
                PowerBookmarkMeta.newBuilder().setShoppingSpecifics(shoppingSpecifics).build();
        doReturn(powerBookmarkMeta).when(bookmarkModel).getPowerBookmarkMeta(URL_BOOKMARK_ID_B);
        doReturn(Arrays.asList(URL_BOOKMARK_ID_D, URL_BOOKMARK_ID_E))
                .when(bookmarkModel)
                .getChildIds(READING_LIST_BOOKMARK_ID);
    }

    private static BookmarkItem makeUrlItem(
            BookmarkId id, String title, GURL url, BookmarkId parentId) {
        return makeUrlItemWithRead(id, title, url, parentId, false);
    }

    private static BookmarkItem makeUrlItemWithRead(
            BookmarkId id, String title, GURL url, BookmarkId parentId, boolean read) {
        long dateAdded = id.getId();
        return new BookmarkItem(id, title, url, false, parentId, false, false, dateAdded, read);
    }

    private static BookmarkItem makeFolderItem(BookmarkId id, String title, BookmarkId parentId) {
        boolean isEditable = ROOT_BOOKMARK_ID.equals(parentId);
        long dateAdded = id.getId();
        return new BookmarkItem(
                id, title, null, true, parentId, isEditable, false, dateAdded, false);
    }
}
