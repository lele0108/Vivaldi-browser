// Copyright 2022 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

import 'chrome://webui-test/mojo_webui_test_support.js';
import 'chrome://bookmarks-side-panel.top-chrome/power_bookmarks_list.js';

import {SortOrder, ViewType} from 'chrome://bookmarks-side-panel.top-chrome/bookmarks.mojom-webui.js';
import {BookmarksApiProxyImpl} from 'chrome://bookmarks-side-panel.top-chrome/bookmarks_api_proxy.js';
import {PowerBookmarkRowElement} from 'chrome://bookmarks-side-panel.top-chrome/power_bookmark_row.js';
import {PowerBookmarksListElement} from 'chrome://bookmarks-side-panel.top-chrome/power_bookmarks_list.js';
import {ShoppingListApiProxyImpl} from 'chrome://bookmarks-side-panel.top-chrome/shared/commerce/shopping_list_api_proxy.js';
import {SpEmptyStateElement} from 'chrome://bookmarks-side-panel.top-chrome/shared/sp_empty_state.js';
import {PageImageServiceBrowserProxy} from 'chrome://resources/cr_components/page_image_service/browser_proxy.js';
import {PageImageServiceHandlerRemote} from 'chrome://resources/cr_components/page_image_service/page_image_service.mojom-webui.js';
import {CrActionMenuElement} from 'chrome://resources/cr_elements/cr_action_menu/cr_action_menu.js';
import {CrInputElement} from 'chrome://resources/cr_elements/cr_input/cr_input.js';
import {loadTimeData} from 'chrome://resources/js/load_time_data.js';
import {flush} from 'chrome://resources/polymer/v3_0/polymer/polymer_bundled.min.js';
import {assertEquals, assertFalse, assertNotEquals, assertTrue} from 'chrome://webui-test/chai_assert.js';
import {flushTasks} from 'chrome://webui-test/polymer_test_util.js';
import {TestMock} from 'chrome://webui-test/test_mock.js';

import {TestShoppingListApiProxy} from './commerce/test_shopping_list_api_proxy.js';
import {TestBookmarksApiProxy} from './test_bookmarks_api_proxy.js';

suite('SidePanelPowerBookmarksListTest', () => {
  let powerBookmarksList: PowerBookmarksListElement;
  let bookmarksApi: TestBookmarksApiProxy;
  let shoppingListApi: TestShoppingListApiProxy;
  let imageServiceHandler: TestMock<PageImageServiceHandlerRemote>&
      PageImageServiceHandlerRemote;

  const folders: chrome.bookmarks.BookmarkTreeNode[] = [
    {
      id: '1',
      parentId: '0',
      title: 'Bookmarks Bar',
      children: [],
    },
    {
      id: '2',
      parentId: '0',
      title: 'Other Bookmarks',
      children: [
        {
          id: '3',
          parentId: '2',
          title: 'First child bookmark',
          url: 'http://child/bookmark/1/',
          dateAdded: 1,
        },
        {
          id: '4',
          parentId: '2',
          title: 'Second child bookmark',
          url: 'http://child/bookmark/2/',
          dateAdded: 3,
        },
        {
          id: '5',
          parentId: '2',
          title: 'Child folder',
          dateAdded: 2,
          children: [
            {
              id: '6',
              parentId: '5',
              title: 'Nested bookmark',
              url: 'http://nested/bookmark/',
              dateAdded: 4,
            },
          ],
        },
      ],
    },
  ];

  function getBookmarkElements(root: HTMLElement): PowerBookmarkRowElement[] {
    return Array.from(root.shadowRoot!.querySelectorAll('power-bookmark-row'));
  }

  setup(async () => {
    document.body.innerHTML = window.trustedTypes!.emptyHTML;

    bookmarksApi = new TestBookmarksApiProxy();
    bookmarksApi.setFolders(JSON.parse(JSON.stringify(folders)));
    BookmarksApiProxyImpl.setInstance(bookmarksApi);

    shoppingListApi = new TestShoppingListApiProxy();
    ShoppingListApiProxyImpl.setInstance(shoppingListApi);

    imageServiceHandler = TestMock.fromClass(PageImageServiceHandlerRemote);
    PageImageServiceBrowserProxy.setInstance(
        new PageImageServiceBrowserProxy(imageServiceHandler));
    imageServiceHandler.setResultFor('getPageImageUrl', Promise.resolve({
      result: {imageUrl: {url: 'https://example.com/image.png'}},
    }));

    loadTimeData.overrideValues({
      sortOrder: SortOrder.kNewest,
      viewType: ViewType.kCompact,
      emptyTitle: 'empty title base',
      emptyTitleSearch: 'empty title search',
      emptyTitleFolder: 'folder is empty',
      emptyBodyFolder: 'folder body',
      emptyTitleGuest: 'guest title',
      emptyBodyGuest: 'guest body',
    });

    powerBookmarksList = document.createElement('power-bookmarks-list');
    document.body.appendChild(powerBookmarksList);

    await bookmarksApi.whenCalled('getFolders');
    await flushTasks();
  });

  test('GetsAndShowsTopLevelBookmarks', () => {
    assertEquals(1, bookmarksApi.getCallCount('getFolders'));
    assertEquals(
        folders[1]!.children!.length + 1,
        getBookmarkElements(powerBookmarksList).length);
  });

  test('DefaultsToSortByNewest', () => {
    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);
    // All folders should come first
    assertEquals('bookmark-1', bookmarkElements[0]!.id);
    assertEquals('bookmark-5', bookmarkElements[1]!.id);
    // Newest URL should come next
    assertEquals('bookmark-4', bookmarkElements[2]!.id);
    // Older URL should be last
    assertEquals('bookmark-3', bookmarkElements[3]!.id);
  });

  test('UpdatesChangedBookmarks', () => {
    const changedBookmark = folders[1]!.children![0]!;
    bookmarksApi.callbackRouter.onChanged.callListeners(changedBookmark.id, {
      title: 'New title',
      url: 'http://new/url',
    });
    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);
    const bookmarkElement = bookmarkElements[3]!;
    assertEquals(
        'New title',
        (bookmarkElement as PowerBookmarkRowElement).bookmark.title);
    assertEquals(
        'http://new/url',
        (bookmarkElement as PowerBookmarkRowElement).bookmark.url);
    assertNotEquals(
        undefined,
        Array
            .from(bookmarkElement.shadowRoot!.querySelectorAll(
                'cr-url-list-item'))
            .find(el => el.title === 'New title'));
  });

  test('AddsCreatedBookmark', async () => {
    bookmarksApi.callbackRouter.onCreated.callListeners('999', {
      id: '999',
      title: 'New bookmark',
      index: 0,
      parentId: folders[1]!.id,
      url: 'http://new/bookmark',
    });
    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(5, bookmarkElements.length);
  });

  test('AddsCreatedBookmarkForNewFolder', () => {
    // Create a new folder without a children array.
    bookmarksApi.callbackRouter.onCreated.callListeners('1000', {
      id: '1000',
      title: 'New folder',
      index: 0,
      parentId: folders[1]!.id,
    });
    flush();

    // Create a new bookmark within that folder.
    bookmarksApi.callbackRouter.onCreated.callListeners('1001', {
      id: '1001',
      title: 'New bookmark in new folder',
      index: 0,
      parentId: '1000',
      url: 'http://google.com',
    });
    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(5, bookmarkElements.length);
    const newFolder = bookmarkElements[0]!;
    assertEquals(
        1, (newFolder as PowerBookmarkRowElement).bookmark.children!.length);
  });

  test('MovesBookmarks', () => {
    const movedBookmark = folders[1]!.children![2]!.children![0]!;
    bookmarksApi.callbackRouter.onMoved.callListeners(movedBookmark.id, {
      index: 0,
      parentId: folders[1]!.id,                   // Moving to other bookmarks.
      oldParentId: folders[1]!.children![2]!.id,  // Moving from child folder.
      oldIndex: 0,
    });
    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(5, bookmarkElements.length);
    const childFolder = bookmarkElements[1]!;
    assertEquals('5', (childFolder as PowerBookmarkRowElement).bookmark.id);
    assertEquals(
        0, (childFolder as PowerBookmarkRowElement).bookmark.children!.length);
  });

  test('MovesBookmarksIntoNewFolder', () => {
    // Create a new folder without a children array.
    bookmarksApi.callbackRouter.onCreated.callListeners('1000', {
      id: '1000',
      title: 'New folder',
      index: 0,
      parentId: folders[1]!.id,
    });
    flush();

    const movedBookmark = folders[1]!.children![2]!.children![0]!;
    bookmarksApi.callbackRouter.onMoved.callListeners(movedBookmark.id, {
      index: 0,
      parentId: '1000',
      oldParentId: folders[1]!.children![2]!.id,
      oldIndex: 0,
    });
    flush();

    const newFolder =
        powerBookmarksList.shadowRoot!.querySelector('#bookmark-1000');
    assertEquals(
        1, (newFolder as PowerBookmarkRowElement).bookmark.children!.length);
  });

  test('SetsCompactDescription', async () => {
    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);
    const folderElement = bookmarkElements[1]!;
    assertEquals('bookmark-5', folderElement.id);

    const urlListItemElement =
        folderElement.shadowRoot!.querySelector('cr-url-list-item');
    const compactDescription = '(1)';
    assertTrue(urlListItemElement!.description!.includes(compactDescription));
  });

  test('SetsExpandedDescription', () => {
    const menu =
        powerBookmarksList.shadowRoot!.querySelector<CrActionMenuElement>(
            '#editMenu')!;
    menu.showAt(powerBookmarksList);
    const visualViewButton: HTMLElement = menu.querySelector('#visualView')!;
    visualViewButton.click();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);
    const folderElement = bookmarkElements[2]!;
    assertEquals('bookmark-4', folderElement.id);

    const urlListItemElement =
        folderElement.shadowRoot!.querySelector('cr-url-list-item');
    const expandedDescription = 'child';
    assertTrue(urlListItemElement!.description!.includes(expandedDescription));
  });

  test('SetsExpandedSearchResultDescription', () => {
    const menu =
        powerBookmarksList.shadowRoot!.querySelector<CrActionMenuElement>(
            '#editMenu')!;
    menu.showAt(powerBookmarksList);
    const visualViewButton: HTMLElement = menu.querySelector('#visualView')!;
    visualViewButton.click();

    const searchField = powerBookmarksList.shadowRoot!.querySelector(
        'cr-toolbar-search-field')!;
    searchField.$.searchInput.value = 'child bookmark';
    searchField.onSearchTermInput();
    searchField.onSearchTermSearch();

    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);
    const folderElement = bookmarkElements[0]!;
    assertEquals('bookmark-4', folderElement.id);

    const urlListItemElement =
        folderElement.shadowRoot!.querySelector('cr-url-list-item');
    const expandedDescription = 'child - All Bookmarks';
    assertTrue(urlListItemElement!.description!.includes(expandedDescription));
  });

  test('RenamesBookmark', async () => {
    const renamedBookmarkId = '4';
    powerBookmarksList.setRenamingIdForTests(renamedBookmarkId);

    await flushTasks();

    const rowElement =
        powerBookmarksList.shadowRoot!.querySelector<PowerBookmarkRowElement>(
            `#bookmark-${renamedBookmarkId}`);
    assertTrue(!!rowElement);
    let input =
        rowElement.shadowRoot!.querySelector<CrInputElement>('cr-input');
    assertTrue(!!input);
    const newName = 'foo';
    input.value = newName;
    input.inputElement.dispatchEvent(new Event('change'));

    await flushTasks();

    // Committing a new input value should rename the bookmark and remove the
    // input.
    assertEquals(1, bookmarksApi.getCallCount('renameBookmark'));
    assertEquals(
        renamedBookmarkId, bookmarksApi.getArgs('renameBookmark')[0][0]);
    assertEquals(newName, bookmarksApi.getArgs('renameBookmark')[0][1]);
    input = rowElement.shadowRoot!.querySelector<CrInputElement>('cr-input');
    assertFalse(!!input);
  });

  test('BlursRenameInput', async () => {
    const renamedBookmarkId = '4';
    powerBookmarksList.setRenamingIdForTests(renamedBookmarkId);

    await flushTasks();

    const rowElement =
        powerBookmarksList.shadowRoot!.querySelector<PowerBookmarkRowElement>(
            `#bookmark-${renamedBookmarkId}`);
    assertTrue(!!rowElement);
    let input =
        rowElement.shadowRoot!.querySelector<CrInputElement>('cr-input');
    assertTrue(!!input);
    input.inputElement.blur();

    await flushTasks();

    // Blurring the input should remove it.
    input = rowElement.shadowRoot!.querySelector<CrInputElement>('cr-input');
    assertFalse(!!input);
  });

  test('ShowsFolderImages', () => {
    const menu =
        powerBookmarksList.shadowRoot!.querySelector<CrActionMenuElement>(
            '#editMenu')!;
    menu.showAt(powerBookmarksList);
    const visualViewButton: HTMLElement = menu.querySelector('#visualView')!;
    visualViewButton.click();

    flush();

    const bookmarkElements = getBookmarkElements(powerBookmarksList);
    assertEquals(4, bookmarkElements.length);

    const bookmarksBarFolderElement = bookmarkElements[0]!;
    const bookmarksBarUrlListItemElement =
        bookmarksBarFolderElement.shadowRoot!.querySelector('cr-url-list-item');
    assertTrue(!!bookmarksBarUrlListItemElement);
    assertEquals(0, bookmarksBarUrlListItemElement.imageUrls.length);

    const otherBookmarksFolderElement = bookmarkElements[1]!;
    const otherBookmarksUrlListItemElement =
        otherBookmarksFolderElement.shadowRoot!.querySelector(
            'cr-url-list-item');
    assertTrue(!!otherBookmarksUrlListItemElement);
    assertNotEquals(0, otherBookmarksUrlListItemElement.imageUrls.length);
  });

  test('ShowsCorrectEmptyState', () => {
    function emptyStateIsHidden(emptyState: SpEmptyStateElement): boolean {
      return emptyState.matches('[hidden], [hidden] *');
    }

    const folderEmptyState = powerBookmarksList.$.folderEmptyState;
    const topLevelEmptyState = powerBookmarksList.$.topLevelEmptyState;
    assertEquals(
        loadTimeData.getString('emptyTitle'), topLevelEmptyState.heading);
    assertEquals(loadTimeData.getString('emptyBody'), topLevelEmptyState.body);

    // Has bookmarks so both empty states should be hidden.
    assertTrue(emptyStateIsHidden(folderEmptyState));
    assertTrue(emptyStateIsHidden(topLevelEmptyState));

    // Opening an empty folder should show the folder empty state.
    getBookmarkElements(powerBookmarksList)[0]!.$.crUrlListItem.click();
    flush();
    assertFalse(emptyStateIsHidden(folderEmptyState));
    assertTrue(emptyStateIsHidden(topLevelEmptyState));

    // A search with no results should show the top level empty state with
    // text specific to search.
    const searchField =
        powerBookmarksList.shadowRoot!.querySelector('cr-toolbar-search-field');
    assertTrue(!!searchField);
    searchField.$.searchInput.value = 'abcdef';
    searchField.onSearchTermSearch();
    flush();
    assertEquals(
        loadTimeData.getString('emptyTitleSearch'), topLevelEmptyState.heading);
    assertTrue(emptyStateIsHidden(folderEmptyState));
    assertFalse(emptyStateIsHidden(topLevelEmptyState));
  });
});
