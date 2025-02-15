// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.bookmarks;

import android.app.ActivityManager;
import android.content.ComponentName;
import android.content.Context;
import android.content.res.Resources;
import android.view.LayoutInflater;
import android.view.View;
import android.view.View.OnAttachStateChangeListener;
import android.view.ViewGroup;

import androidx.annotation.LayoutRes;
import androidx.annotation.NonNull;
import androidx.annotation.VisibleForTesting;
import androidx.recyclerview.widget.RecyclerView;
import androidx.recyclerview.widget.RecyclerView.ItemAnimator;

import org.chromium.base.ContextUtils;
import org.chromium.base.metrics.RecordUserAction;
import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.ObservableSupplierImpl;
import org.chromium.base.supplier.OneshotSupplierImpl;
import org.chromium.chrome.R;
import org.chromium.chrome.browser.bookmarks.BookmarkListEntry.ViewType;
import org.chromium.chrome.browser.bookmarks.BookmarkUiPrefs.BookmarkRowDisplayPref;
import org.chromium.chrome.browser.commerce.ShoppingFeatures;
import org.chromium.chrome.browser.commerce.ShoppingServiceFactory;
import org.chromium.chrome.browser.flags.ChromeFeatureList;
import org.chromium.chrome.browser.profiles.Profile;
import org.chromium.chrome.browser.ui.messages.snackbar.SnackbarManager;
import org.chromium.chrome.browser.ui.native_page.BasicNativePage;
import org.chromium.components.bookmarks.BookmarkId;
import org.chromium.components.browser_ui.modaldialog.AppModalPresenter;
import org.chromium.components.browser_ui.util.ConversionUtils;
import org.chromium.components.browser_ui.util.GlobalDiscardableReferencePool;
import org.chromium.components.browser_ui.widget.dragreorder.DragReorderableRecyclerViewAdapter;
import org.chromium.components.browser_ui.widget.gesture.BackPressHandler;
import org.chromium.components.browser_ui.widget.selectable_list.SelectableListLayout;
import org.chromium.components.browser_ui.widget.selectable_list.SelectableListToolbar.SearchDelegate;
import org.chromium.components.browser_ui.widget.selectable_list.SelectionDelegate;
import org.chromium.components.favicon.LargeIconBridge;
import org.chromium.components.image_fetcher.ImageFetcher;
import org.chromium.components.image_fetcher.ImageFetcherConfig;
import org.chromium.components.image_fetcher.ImageFetcherFactory;
import org.chromium.ui.KeyboardVisibilityDelegate;
import org.chromium.ui.modaldialog.ModalDialogManager;
import org.chromium.ui.modaldialog.ModalDialogManager.ModalDialogType;
import org.chromium.ui.modelutil.MVCListAdapter.ModelList;

// Vivaldi
import org.vivaldi.browser.bookmarks.BookmarkDialogDelegate;
import org.vivaldi.browser.bookmarks.VivaldiBookmarksPageObserver;

/** Responsible for setting up sub-components and routing incoming/outgoing signals */
// TODO(crbug.com/1446506): Add a new coordinator so this class doesn't own everything.
public class BookmarkManagerCoordinator
        implements SearchDelegate, BackPressHandler, OnAttachStateChangeListener {
    private static final int FAVICON_MAX_CACHE_SIZE_BYTES =
            10 * ConversionUtils.BYTES_PER_MEGABYTE; // 10MB

    private final SelectionDelegate<BookmarkId> mSelectionDelegate = new SelectionDelegate<>() {
        @Override
        public boolean toggleSelectionForItem(BookmarkId bookmark) {
            if (mBookmarkModel.getBookmarkById(bookmark) != null
                    && !mBookmarkModel.getBookmarkById(bookmark).isEditable()) {
                return false;
            }
            return super.toggleSelectionForItem(bookmark);
        }
    };

    private final ObservableSupplierImpl<Boolean> mBackPressStateSupplier =
            new ObservableSupplierImpl<>();
    private final ViewGroup mMainView;
    private final SelectableListLayout<BookmarkId> mSelectableListLayout;
    private final RecyclerView mRecyclerView;
    private final BookmarkOpener mBookmarkOpener;
    private final BookmarkToolbarCoordinator mBookmarkToolbarCoordinator;
    private final BookmarkManagerMediator mMediator;
    private final ImageFetcher mImageFetcher;
    private final SnackbarManager mSnackbarManager;
    private final BookmarkPromoHeader mPromoHeaderManager;
    private final BookmarkModel mBookmarkModel;
    private final Profile mProfile;
    private final BookmarkUiPrefs mBookmarkUiPrefs;
    private final ModalDialogManager mModalDialogManager;

    /**
     * Creates an instance of {@link BookmarkManagerCoordinator}. It also initializes resources,
     * bookmark models and jni bridges.
     * @param context The current {@link Context} used to obtain resources or inflate views.
     * @param openBookmarkComponentName The component to use when opening a bookmark.
     * @param isDialogUi Whether the main bookmarks UI will be shown in a dialog, not a NativePage.
     * @param isIncognito Whether the tab model loading the bookmark manager is for incognito mode.
     * @param snackbarManager The {@link SnackbarManager} used to display snackbars.
     * @param profile The profile which the manager is running in.
     * @param bookmarkUiPrefs Manages prefs for bookmarks ui.
     */
    public BookmarkManagerCoordinator(Context context, ComponentName openBookmarkComponentName,
            boolean isDialogUi, boolean isIncognito, SnackbarManager snackbarManager,
            Profile profile, BookmarkUiPrefs bookmarkUiPrefs) {
        mProfile = profile;
        mImageFetcher =
                ImageFetcherFactory.createImageFetcher(ImageFetcherConfig.IN_MEMORY_WITH_DISK_CACHE,
                        profile.getProfileKey(), GlobalDiscardableReferencePool.getReferencePool());
        mSnackbarManager = snackbarManager;

        mMainView = (ViewGroup) LayoutInflater.from(context).inflate(R.layout.bookmark_main, null);
        mBookmarkModel = BookmarkModel.getForProfile(profile);
        mBookmarkOpener = new BookmarkOpener(mBookmarkModel, context, openBookmarkComponentName);
        if (ShoppingFeatures.isShoppingListEligible()) {
            ShoppingServiceFactory.getForProfile(profile).scheduleSavedProductUpdate();
        }
        mBookmarkUiPrefs = bookmarkUiPrefs;

        @SuppressWarnings("unchecked")
        SelectableListLayout<BookmarkId> selectableList =
                mMainView.findViewById(R.id.selectable_list);
        mSelectableListLayout = selectableList;
        if (ChromeFeatureList.isEnabled(ChromeFeatureList.EMPTY_STATES)) {
            mSelectableListLayout.initializeEmptyStateView(
                    R.drawable.bookmark_empty_state_illustration,
                    R.string.bookmark_manager_empty_state,
                    R.string.bookmark_manager_back_to_page_by_adding_bookmark);
        } else {
            mSelectableListLayout.initializeEmptyView(R.string.bookmarks_folder_empty);
        }

        ModelList modelList = new ModelList();
        DragReorderableRecyclerViewAdapter dragReorderableRecyclerViewAdapter =
                new DragReorderableRecyclerViewAdapter(context, modelList);
        mRecyclerView =
                mSelectableListLayout.initializeRecyclerView(dragReorderableRecyclerViewAdapter);

        // Disable everything except move animations. Switching between folders should be as
        // seamless as possible without flickering caused by these animations. While dragging
        // should still pick up the slide animation from moves.
        ItemAnimator itemAnimator = mRecyclerView.getItemAnimator();
        itemAnimator.setChangeDuration(0);
        itemAnimator.setAddDuration(0);
        itemAnimator.setRemoveDuration(0);

        mModalDialogManager =
                new ModalDialogManager(new AppModalPresenter(context), ModalDialogType.APP);

        // Using OneshotSupplier as an alternative to a 2-step initialization process.
        OneshotSupplierImpl<BookmarkDelegate> bookmarkDelegateSupplier =
                new OneshotSupplierImpl<>();
        mBookmarkToolbarCoordinator = new BookmarkToolbarCoordinator(context, mSelectableListLayout,
                mSelectionDelegate, /*searchDelegate=*/this, dragReorderableRecyclerViewAdapter,
                isDialogUi, bookmarkDelegateSupplier, mBookmarkModel, mBookmarkOpener,
                mBookmarkUiPrefs, mModalDialogManager);
        mSelectableListLayout.configureWideDisplayStyle();

        LargeIconBridge largeIconBridge = new LargeIconBridge(mProfile);
        largeIconBridge.createCache(computeCacheMaxSize());

        Resources res = context.getResources();
        final @BookmarkRowDisplayPref int displayPref =
                mBookmarkUiPrefs.getBookmarkRowDisplayPref();
        BookmarkImageFetcher bookmarkImageFetcher = new BookmarkImageFetcher(context,
                mBookmarkModel,
                ImageFetcherFactory.createImageFetcher(
                        ImageFetcherConfig.DISK_CACHE_ONLY, mProfile.getProfileKey()),
                largeIconBridge, BookmarkUtils.getRoundedIconGenerator(context, displayPref),
                BookmarkUtils.getImageIconSize(res, displayPref),
                BookmarkUtils.getFaviconDisplaySize(res, displayPref));

        BookmarkUndoController bookmarkUndoController =
                new BookmarkUndoController(context, mBookmarkModel, snackbarManager);
        mMediator = new BookmarkManagerMediator(context, mBookmarkModel, mBookmarkOpener,
                mSelectableListLayout, mSelectionDelegate, mRecyclerView,
                dragReorderableRecyclerViewAdapter, largeIconBridge, isDialogUi, isIncognito,
                mBackPressStateSupplier, mProfile, bookmarkUndoController, modelList,
                mBookmarkUiPrefs, this::hideKeyboard, bookmarkImageFetcher,
                ShoppingServiceFactory.getForProfile(mProfile), mSnackbarManager);
        mPromoHeaderManager = mMediator.getPromoHeaderManager();

        bookmarkDelegateSupplier.set(/*bookmarkDelegate=*/mMediator);

        mMainView.addOnAttachStateChangeListener(this);

        dragReorderableRecyclerViewAdapter.registerType(ViewType.PERSONALIZED_SIGNIN_PROMO,
                this::buildPersonalizedPromoView,
                BookmarkManagerViewBinder::bindPersonalizedPromoView);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.PERSONALIZED_SYNC_PROMO,
                this::buildPersonalizedPromoView,
                BookmarkManagerViewBinder::bindPersonalizedPromoView);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.SYNC_PROMO,
                this::buildLegacyPromoView, BookmarkManagerViewBinder::bindLegacyPromoView);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.SECTION_HEADER,
                BookmarkManagerCoordinator::buildSectionHeaderView,
                BookmarkManagerViewBinder::bindSectionHeaderView);
        dragReorderableRecyclerViewAdapter.registerDraggableType(ViewType.FOLDER,
                this::buildAndInitBookmarkFolderView,
                BookmarkManagerViewBinder::bindBookmarkFolderView,
                BookmarkManagerViewBinder::bindDraggableViewHolder,
                mMediator.getDraggabilityProvider());
        dragReorderableRecyclerViewAdapter.registerDraggableType(ViewType.BOOKMARK,
                this::buildAndInitBookmarkItemRow, BookmarkManagerViewBinder::bindBookmarkItemView,
                BookmarkManagerViewBinder::bindDraggableViewHolder,
                mMediator.getDraggabilityProvider());
        dragReorderableRecyclerViewAdapter.registerDraggableType(ViewType.SHOPPING_POWER_BOOKMARK,
                this::buildAndInitShoppingItemView, BookmarkManagerViewBinder::bindShoppingItemView,
                BookmarkManagerViewBinder::bindDraggableViewHolder,
                mMediator.getDraggabilityProvider());
        dragReorderableRecyclerViewAdapter.registerType(ViewType.DIVIDER,
                BookmarkManagerCoordinator::buildDividerView,
                BookmarkManagerViewBinder::bindDividerView);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.SHOPPING_FILTER,
                BookmarkManagerCoordinator::buildShoppingFilterView,
                BookmarkManagerViewBinder::bindShoppingFilterView);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.IMPROVED_BOOKMARK_VISUAL,
                this::buildAndInitVisualImprovedBookmarkRow, ImprovedBookmarkRowViewBinder::bind);
        dragReorderableRecyclerViewAdapter.registerType(ViewType.IMPROVED_BOOKMARK_COMPACT,
                this::buildAndInitCompactImprovedBookmarkRow, ImprovedBookmarkRowViewBinder::bind);
        dragReorderableRecyclerViewAdapter.registerType(
                ViewType.SEARCH_BOX, this::buildSearchBoxRow, BookmarkSearchBoxRowViewBinder::bind);

        RecordUserAction.record("MobileBookmarkManagerOpen");
        if (!isDialogUi) {
            RecordUserAction.record("MobileBookmarkManagerPageOpen");
        }
    }

    /** Vivaldi **/
    public BookmarkManagerCoordinator(Context context, ComponentName openBookmarkComponentName,
                                      boolean isDialogUi, boolean isIncognito,
                                      SnackbarManager snackbarManager, Profile profile,
                                      BookmarkUiPrefs bookmarkUiPrefs,
                                      BookmarkDialogDelegate dialogDelegate) {
        this(context, openBookmarkComponentName, isDialogUi, isIncognito,
                snackbarManager, profile, bookmarkUiPrefs);
        setToolbarDialogDelegate(dialogDelegate);
    }

    // Public API implementation.

    /**
     * Destroys and cleans up itself. This must be called after done using this class.
     */
    public void onDestroyed() {
        RecordUserAction.record("MobileBookmarkManagerClose");
        mMainView.removeOnAttachStateChangeListener(this);
        mSelectableListLayout.onDestroyed();
        mMediator.onDestroy();
    }

    /** Returns the view that shows the main bookmarks UI. */
    public View getView() {
        return mMainView;
    }

    /**
     * Sets the listener that reacts upon the change of the UI state of bookmark manager.
     */
    // TODO(crbug.com/1418859): Create abstraction between BookmarkManager & BasicNativePage.
    public void setBasicNativePage(BasicNativePage nativePage) {
        mMediator.setBasicNativePage(nativePage);
    }

    /**
     * Updates UI based on the new URL. If the bookmark model is not loaded yet, cache the url and
     * it will be picked up later when the model is loaded. This method is supposed to align with
     * {@link BookmarkPage#updateForUrl(String)}
     * <p>
     * @param url The url to navigate to.
     */
    public void updateForUrl(String url) {
        mMediator.updateForUrl(url);
    }

    /**
     * Called when the user presses the back key. This is only going to be called on Phone.
     * @return True if manager handles this event, false if it decides to ignore.
     */
    public boolean onBackPressed() {
        return mMediator.onBackPressed();
    }

    /** Opens the given BookmarkId. */
    public void openBookmark(BookmarkId bookmarkId) {
        mMediator.openBookmark(bookmarkId);
    }

    // OnAttachStateChangeListener implementation.

    @Override
    public void onViewAttachedToWindow(@NonNull View view) {
        mMediator.onAttachedToWindow();
    }

    @Override
    public void onViewDetachedFromWindow(@NonNull View view) {
        mMediator.onDetachedFromWindow();
    }

    // BackPressHandler implementation.

    @Override
    public @BackPressResult int handleBackPress() {
        return onBackPressed() ? BackPressResult.SUCCESS : BackPressResult.FAILURE;
    }

    @Override
    public ObservableSupplier<Boolean> getHandleBackPressChangedSupplier() {
        return mBackPressStateSupplier;
    }

    // SearchDelegate implementation.

    @Override
    public void onSearchTextChanged(String query) {
        mMediator.search(query);
    }

    @Override
    public void onEndSearch() {
        mMediator.onEndSearch();
    }

    // Private methods.

    private int computeCacheMaxSize() {
        ActivityManager activityManager =
                ((ActivityManager) ContextUtils.getApplicationContext().getSystemService(
                        Context.ACTIVITY_SERVICE));
        return Math.min(activityManager.getMemoryClass() / 4 * ConversionUtils.BYTES_PER_MEGABYTE,
                FAVICON_MAX_CACHE_SIZE_BYTES);
    }

    @VisibleForTesting
    View buildPersonalizedPromoView(ViewGroup parent) {
        return mPromoHeaderManager.createPersonalizedSigninAndSyncPromoHolder(parent);
    }

    @VisibleForTesting
    View buildLegacyPromoView(ViewGroup parent) {
        return mPromoHeaderManager.createSyncPromoHolder(parent);
    }

    static @VisibleForTesting View buildSectionHeaderView(ViewGroup parent) {
        return inflate(parent, org.chromium.chrome.R.layout.bookmark_section_header);
    }

    private static BookmarkFolderRow buildBookmarkFolderView(ViewGroup parent) {
        BookmarkFolderRow bookmarkFolderRow = BookmarkFolderRow.buildView(
                parent.getContext(), BookmarkFeatures.isLegacyBookmarksVisualRefreshEnabled());
        return bookmarkFolderRow;
    }

    static @VisibleForTesting BookmarkItemRow buildBookmarkItemView(ViewGroup parent) {
        BookmarkItemRow bookmarkItemRow = BookmarkItemRow.buildView(
                parent.getContext(), BookmarkFeatures.isLegacyBookmarksVisualRefreshEnabled());
        return bookmarkItemRow;
    }

    static @VisibleForTesting PowerBookmarkShoppingItemRow buildShoppingItemView(ViewGroup parent) {
        PowerBookmarkShoppingItemRow powerBookmarkShoppingItemRow =
                PowerBookmarkShoppingItemRow.buildView(parent.getContext(),
                        BookmarkFeatures.isLegacyBookmarksVisualRefreshEnabled());
        return powerBookmarkShoppingItemRow;
    }

    static @VisibleForTesting View buildDividerView(ViewGroup parent) {
        return inflate(parent, org.chromium.chrome.R.layout.horizontal_divider);
    }

    static @VisibleForTesting View buildShoppingFilterView(ViewGroup parent) {
        return inflate(parent, org.chromium.chrome.R.layout.shopping_filter_row);
    }

    ImprovedBookmarkRow buildAndInitCompactImprovedBookmarkRow(ViewGroup parent) {
        ImprovedBookmarkRow row = ImprovedBookmarkRow.buildView(parent.getContext(), false);
        row.setSelectionDelegate(mSelectionDelegate);
        return row;
    }

    ImprovedBookmarkRow buildAndInitVisualImprovedBookmarkRow(ViewGroup parent) {
        ImprovedBookmarkRow row = ImprovedBookmarkRow.buildView(parent.getContext(), true);
        row.setSelectionDelegate(mSelectionDelegate);
        return row;
    }

    View buildSearchBoxRow(ViewGroup parent) {
        return inflate(parent, org.chromium.chrome.R.layout.bookmark_search_box_row);
    }

    private static View inflate(ViewGroup parent, @LayoutRes int layoutId) {
        Context context = parent.getContext();
        return LayoutInflater.from(context).inflate(layoutId, parent, false);
    }

    @VisibleForTesting
    BookmarkFolderRow buildAndInitBookmarkFolderView(ViewGroup parent) {
        BookmarkFolderRow bookmarkFolderRow = buildBookmarkFolderView(parent);
        bookmarkFolderRow.onDelegateInitialized(mMediator);
        return bookmarkFolderRow;
    }

    @VisibleForTesting
    BookmarkItemRow buildAndInitBookmarkItemRow(ViewGroup parent) {
        BookmarkItemRow bookmarkItemRow = buildBookmarkItemView(parent);
        bookmarkItemRow.onDelegateInitialized(mMediator);
        return bookmarkItemRow;
    }

    @VisibleForTesting
    PowerBookmarkShoppingItemRow buildAndInitShoppingItemView(ViewGroup parent) {
        PowerBookmarkShoppingItemRow powerBookmarkShoppingItemRow = buildShoppingItemView(parent);
        powerBookmarkShoppingItemRow.onDelegateInitialized(mMediator);
        // TODO(https://crbug.com/1416611): Move init to view binding.
        powerBookmarkShoppingItemRow.init(
                mImageFetcher, mBookmarkModel, mSnackbarManager, mProfile);
        return powerBookmarkShoppingItemRow;
    }

    private void hideKeyboard() {
        KeyboardVisibilityDelegate.getInstance().hideKeyboard(mMainView);
    }

    // Testing methods.

    public BookmarkToolbarCoordinator getToolbarCoordinatorForTesting() {
        return mBookmarkToolbarCoordinator;
    }

    public BookmarkToolbar getToolbarForTesting() {
        return mBookmarkToolbarCoordinator.getToolbarForTesting(); // IN-TEST
    }

    public BookmarkUndoController getUndoControllerForTesting() {
        return mMediator.getUndoControllerForTesting();
    }

    public RecyclerView getRecyclerViewForTesting() {
        return mRecyclerView;
    }

    public static void preventLoadingForTesting(boolean preventLoading) {
        BookmarkManagerMediator.preventLoadingForTesting(preventLoading);
    }

    public void finishLoadingForTesting() {
        mMediator.finishLoadingForTesting(); // IN-TEST
    }

    public BookmarkOpener getBookmarkOpenerForTesting() {
        return mBookmarkOpener;
    }

    public BookmarkDelegate getBookmarkDelegateForTesting() {
        return mMediator;
    }

    public TestingDelegate getTestingDelegate() {
        return mMediator;
    }

    /** Vivaldi **/
    public boolean isCurrentTrashFolder() {
        if (mBookmarkModel.isBookmarkModelLoaded())
            return mBookmarkModel.getTrashFolderId().equals(mMediator.getCurrentFolder());
        else return false;
    }
    public void openSearchUI() {}
    public BookmarkModel getModel() { return mBookmarkModel; }
    public String getCurrentUrl() { return mMediator.getCurrentUrl(); }
    public BookmarkId getCurrentFolder() { return mMediator.getCurrentFolder(); }
    public void clearSelection() { }
    public void setToolbarDialogDelegate(BookmarkDialogDelegate bookmarkDialogDelegate) {
        mBookmarkToolbarCoordinator.setBookmarkDialogDelegate(bookmarkDialogDelegate);
    }
    public void setBookmarksPageObserver(VivaldiBookmarksPageObserver observer) {
        mMediator.setBookmarksPageObserver(observer);
    }
    public void removeBookmarksPageObserver() {
        mMediator.removeBookmarksPageObserver();
    }
}
