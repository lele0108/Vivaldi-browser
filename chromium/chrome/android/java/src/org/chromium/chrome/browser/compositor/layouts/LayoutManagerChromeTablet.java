// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.compositor.layouts;

import android.view.View;
import android.view.ViewGroup;

import org.chromium.base.supplier.ObservableSupplier;
import org.chromium.base.supplier.Supplier;
import org.chromium.chrome.browser.compositor.LayerTitleCache;
import org.chromium.chrome.browser.compositor.layouts.content.TabContentManager;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutHelperManager;
import org.chromium.chrome.browser.compositor.overlays.strip.StripLayoutHelperManager.TabModelStartupInfo;
import org.chromium.chrome.browser.device.DeviceClassManager;
import org.chromium.chrome.browser.layouts.LayoutType;
import org.chromium.chrome.browser.lifecycle.ActivityLifecycleDispatcher;
import org.chromium.chrome.browser.multiwindow.MultiInstanceManager;
import org.chromium.chrome.browser.tab.TabLaunchType;
import org.chromium.chrome.browser.tabmodel.TabCreatorManager;
import org.chromium.chrome.browser.tabmodel.TabModelSelector;
import org.chromium.chrome.browser.tasks.tab_management.TabSwitcher;
import org.chromium.chrome.browser.theme.ThemeColorProvider;
import org.chromium.chrome.browser.theme.ThemeColorProvider.ThemeColorObserver;
import org.chromium.chrome.browser.theme.TopUiThemeColorProvider;
import org.chromium.chrome.browser.toolbar.ControlContainer;
import org.chromium.chrome.features.start_surface.StartSurface;
import org.chromium.components.browser_ui.widget.scrim.ScrimCoordinator;
import org.chromium.ui.resources.dynamics.DynamicResourceLoader;

import java.util.concurrent.Callable;

// Vivaldi
import java.util.ArrayList;
import java.util.List;
import org.chromium.chrome.browser.ChromeApplicationImpl;

/**
 * {@link LayoutManagerChromeTablet} is the specialization of {@link LayoutManagerChrome} for
 * the tablet.
 */
public class LayoutManagerChromeTablet extends LayoutManagerChrome {
    // Tab Switcher
    private final ScrimCoordinator mScrimCoordinator;
    private final Callable<ViewGroup> mCreateStartSurfaceCallable;
    // Tab Strip
    private StripLayoutHelperManager mTabStripLayoutHelperManager;

    // Vivaldi
    private final List<StripLayoutHelperManager> mTabStrips = new ArrayList<>();

    // Theme Color
    TopUiThemeColorProvider mTopUiThemeColorProvider;
    ThemeColorObserver mThemeColorObserver;

    // Internal State
    /** A {@link TitleCache} instance that stores all title/favicon bitmaps as CC resources. */
    // This cache should not be cleared in LayoutManagerImpl#emptyCachesExcept(), since that method
    // is currently called when returning to the static layout, which is when these titles will be
    // visible. See https://crbug.com/1329293.
    protected LayerTitleCache mLayerTitleCache;

    private final Supplier<StartSurface> mStartSurfaceSupplier;
    private final Supplier<TabSwitcher> mTabSwitcherSupplier;

    /**
     * Creates an instance of a {@link LayoutManagerChromePhone}.
     * @param host                     A {@link LayoutManagerHost} instance.
     * @param contentContainer A {@link ViewGroup} for Android views to be bound to.
     * @param startSurfaceSupplier Supplier for an interface to talk to the Grid Tab Switcher when
     *         Start surface refactor is disabled.
     * @param tabSwitcherSupplier Supplier for an interface to talk to the Grid Tab Switcher when
     *         Start surface refactor is enabled.
     * @param tabContentManagerSupplier Supplier of the {@link TabContentManager} instance.
     * @param topUiThemeColorProvider {@link ThemeColorProvider} for top UI.
     * @param tabSwitcherViewHolder {@link ViewGroup} used by tab switcher layout to show scrim
     *         when overview is visible.
     * @param scrimCoordinator {@link ScrimCoordinator} to show/hide scrim.
     * @param lifecycleDispatcher @{@link ActivityLifecycleDispatcher} to be passed to TabStrip
     *         helper.
     * @param delayedStartSurfaceCallable Callable to create StartSurface/GTS views.
     * @param multiInstanceManager @{link MultiInstanceManager} passed to @{link StripLayoutHelper}
     *         to support tab drag and drop.
     * @param toolbarContainerView @{link View} passed to @{link StripLayoutHelper} to support tab
     *         drag and drop.
     */
    public LayoutManagerChromeTablet(LayoutManagerHost host, ViewGroup contentContainer,
            Supplier<StartSurface> startSurfaceSupplier, Supplier<TabSwitcher> tabSwitcherSupplier,
            ObservableSupplier<TabContentManager> tabContentManagerSupplier,
            Supplier<TopUiThemeColorProvider> topUiThemeColorProvider,
            ObservableSupplier<TabModelStartupInfo> tabModelStartupInfoSupplier,
            ViewGroup tabSwitcherViewHolder, ScrimCoordinator scrimCoordinator,
            ActivityLifecycleDispatcher lifecycleDispatcher,
            Callable<ViewGroup> delayedStartSurfaceCallable,
            MultiInstanceManager multiInstanceManager, View toolbarContainerView) {
        super(host, contentContainer, startSurfaceSupplier, tabSwitcherSupplier,
                tabContentManagerSupplier, topUiThemeColorProvider, tabSwitcherViewHolder,
                scrimCoordinator);
        mStartSurfaceSupplier = startSurfaceSupplier;
        mTabSwitcherSupplier = tabSwitcherSupplier;

        // Note(david@vivaldi.com): The |StripLayoutHelperManager|s for Vivaldi are instantiated
        // below.
        if (!ChromeApplicationImpl.isVivaldi())
        mTabStripLayoutHelperManager = new StripLayoutHelperManager(host.getContext(), host, this,
                mHost.getLayoutRenderHost(),
                ()
                        -> mLayerTitleCache,
                tabModelStartupInfoSupplier, lifecycleDispatcher, multiInstanceManager,
                toolbarContainerView);

        // Note(david@vivaldi.com): We create two tab strips here. The first one is the main strip.
        // The second one is the stack strip.
        for (int i = 0; i < 2; i++) {
            mTabStrips.add(new StripLayoutHelperManager(host.getContext(), host, this,
                    mHost.getLayoutRenderHost(),
                    ()
                            -> mLayerTitleCache,
                    tabModelStartupInfoSupplier, lifecycleDispatcher, multiInstanceManager,
                    toolbarContainerView));
            mTabStrips.get(i).setIsStackStrip(i != 0);
            addObserver(mTabStrips.get(i).getTabSwitcherObserver());
            addSceneOverlay(mTabStrips.get(i));
        }

        mScrimCoordinator = scrimCoordinator;
        mCreateStartSurfaceCallable = delayedStartSurfaceCallable;
        if (!ChromeApplicationImpl.isVivaldi())
        addObserver(mTabStripLayoutHelperManager.getTabSwitcherObserver());

        setNextLayout(null, true);
    }

    @Override
    public void destroy() {
        super.destroy();

        // Vivaldi
        for (int i = 0; i < 2; i++) mTabStrips.get(i).destroy();
        mTabStrips.clear();

        if (mTopUiThemeColorProvider != null && mThemeColorObserver != null) {
            mTopUiThemeColorProvider.removeThemeColorObserver(mThemeColorObserver);
            mTopUiThemeColorProvider = null;
            mThemeColorObserver = null;
        }
    }

    @Override
    protected void tabCreated(int id, int sourceId, @TabLaunchType int launchType,
            boolean incognito, boolean willBeSelected, float originX, float originY) {
        if (getBrowserControlsManager() != null) {
            getBrowserControlsManager().getBrowserVisibilityDelegate().showControlsTransient();
        }
        super.tabCreated(id, sourceId, launchType, incognito, willBeSelected, originX, originY);
    }

    @Override
    public void onTabsAllClosing(boolean incognito) {
        if (getActiveLayout() == mStaticLayout && !incognito) {
            showLayout(LayoutType.TAB_SWITCHER, /*animate=*/false);
        }
        super.onTabsAllClosing(incognito);
    }

    @Override
    protected void tabModelSwitched(boolean incognito) {
        super.tabModelSwitched(incognito);
        getTabModelSelector().commitAllTabClosures();
        if (getActiveLayout() == mStaticLayout && !incognito
                && getTabModelSelector().getModel(false).getCount() == 0) {
            showLayout(LayoutType.TAB_SWITCHER, /*animate=*/false);
        }
    }

    @Override
    public void init(TabModelSelector selector, TabCreatorManager creator,
            ControlContainer controlContainer,
            DynamicResourceLoader dynamicResourceLoader,
            TopUiThemeColorProvider topUiColorProvider) {

        // Vivaldi
        for (int i = 0; i < 2; i++) mTabStrips.get(i).setTabModelSelector(selector, creator);

        super.init(selector, creator, controlContainer, dynamicResourceLoader, topUiColorProvider);
        if (DeviceClassManager.enableLayerDecorationCache()) {
            mLayerTitleCache = new LayerTitleCache(mHost.getContext(), getResourceManager());
            // TODO: TitleCache should be a part of the ResourceManager.
            mLayerTitleCache.setTabModelSelector(selector);
        }

        if (mTabStripLayoutHelperManager != null) {
            mTabStripLayoutHelperManager.setTabModelSelector(selector, creator);
        }
    }

    @Override
    public void showLayout(int layoutType, boolean animate) {
        if (layoutType == LayoutType.TAB_SWITCHER && mOverviewLayout == null
                && mTabSwitcherLayout == null) {
            try {
                if (!mStartSurfaceSupplier.hasValue() && !mTabSwitcherSupplier.hasValue()) {
                    final ViewGroup containerView = mCreateStartSurfaceCallable.call();
                    createOverviewLayout(mStartSurfaceSupplier.get(), mTabSwitcherSupplier.get(),
                            mScrimCoordinator, containerView);
                    mThemeColorObserver =
                            (color, shouldAnimate) -> containerView.setBackgroundColor(color);
                    mTopUiThemeColorProvider = getTopUiThemeColorProvider().get();
                    mTopUiThemeColorProvider.addThemeColorObserver(mThemeColorObserver);
                }
            } catch (Exception e) {
                throw new RuntimeException("Failed to initialize start surface.", e);
            }
        }
        super.showLayout(layoutType, animate);
    }

    @Override
    public void initLayoutTabFromHost(final int tabId) {
        if (mLayerTitleCache != null) {
            mLayerTitleCache.remove(tabId);
        }
        super.initLayoutTabFromHost(tabId);
    }

    @Override
    public void releaseTabLayout(int id) {
        mLayerTitleCache.remove(id);
        super.releaseTabLayout(id);
    }

    @Override
    public void releaseResourcesForTab(int tabId) {
        super.releaseResourcesForTab(tabId);
        mLayerTitleCache.remove(tabId);
    }

    @Override
    public StripLayoutHelperManager getStripLayoutHelperManager() {
        // Vivaldi: We always return our main strip here.
        if (ChromeApplicationImpl.isVivaldi()) return mTabStrips.get(0);
        return mTabStripLayoutHelperManager;
    }
}
