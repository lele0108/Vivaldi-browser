// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.quick_delete;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.doNothing;
import static org.mockito.Mockito.doReturn;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;
import static org.mockito.MockitoAnnotations.initMocks;

import androidx.test.filters.SmallTest;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.mockito.Mock;
import org.robolectric.annotation.Config;
import org.robolectric.annotation.LooperMode;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.tab.MockTab;
import org.chromium.chrome.browser.tab.Tab;
import org.chromium.chrome.browser.tab.state.CriticalPersistedTabData;
import org.chromium.chrome.browser.tabmodel.TabModel;

import java.util.ArrayList;
import java.util.List;

/**
 * Robolectric tests for {@link QuickDeleteTabsFilter}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
@LooperMode(LooperMode.Mode.PAUSED)
public class QuickDeleteTabsFilterTest {
    private static final long INITIAL_TIME_IN_MS = 1000;
    private static final long FIFTEEN_MINUTES_IN_MS = 15 * 60 * 1000;

    private QuickDeleteTabsFilter mQuickDeleteTabsFilter;
    private final List<MockTab> mMockTabList = new ArrayList<>();

    @Mock
    private TabModel mTabModelMock;

    private void createTabsAndUpdateTabModel(int countOfTabs) {
        // Create tabs.
        for (int id = 0; id < countOfTabs; id++) {
            MockTab mockTab = new MockTab(id, /*incognito=*/false);
            CriticalPersistedTabData mockTabData = new CriticalPersistedTabData(mockTab);
            mockTabData.setRootId(id);
            mockTab.getUserDataHost().setUserData(CriticalPersistedTabData.class, mockTabData);
            mMockTabList.add(mockTab);
        }
        // Update the tab model.
        doReturn(countOfTabs).when(mTabModelMock).getCount();
        for (int i = 0; i < countOfTabs; i++) {
            when(mTabModelMock.getTabAt(i)).thenReturn(mMockTabList.get(i));
        }
    }

    @Before
    public void setUp() {
        initMocks(this);
        doReturn(false).when(mTabModelMock).isIncognito();
    }

    @Test(expected = AssertionError.class)
    @SmallTest
    public void testIncognitoTabModel_ThrowsAssertionError() {
        doReturn(true).when(mTabModelMock).isIncognito();
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);
    }

    @Test
    @SmallTest
    public void testAddOneTabOutside15MinutesRange() {
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);

        createTabsAndUpdateTabModel(1);
        CriticalPersistedTabData.from(mMockTabList.get(0))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS);

        // 15 minutes passes...
        mQuickDeleteTabsFilter.setCurrentTimeForTesting(INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        List<Tab> filteredTabs = mQuickDeleteTabsFilter.getListOfTabsToBeClosed();
        assertTrue(filteredTabs.isEmpty());
    }

    @Test
    @SmallTest
    public void testAddOneTabWithin15MinutesRange() {
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);

        createTabsAndUpdateTabModel(1);
        CriticalPersistedTabData.from(mMockTabList.get(0))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS + 10);

        // 15 minutes passes...
        mQuickDeleteTabsFilter.setCurrentTimeForTesting(INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        List<Tab> filteredTabs = mQuickDeleteTabsFilter.getListOfTabsToBeClosed();
        assertEquals(1, filteredTabs.size());
        assertEquals(mMockTabList.get(0), filteredTabs.get(0));
    }

    @Test
    @SmallTest
    public void testCustomTab_NotConsideredInFlow() {
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);

        createTabsAndUpdateTabModel(1);
        // Mock the tab as custom tab.
        mMockTabList.get(0).setIsCustomTab(true);
        // Make the custom tab in the right period for consideration for quick delete.
        CriticalPersistedTabData.from(mMockTabList.get(0))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS + 10);

        // 15 minutes passes...
        mQuickDeleteTabsFilter.setCurrentTimeForTesting(INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        List<Tab> filteredTabs = mQuickDeleteTabsFilter.getListOfTabsToBeClosed();
        assertTrue(filteredTabs.isEmpty());
    }

    @Test
    @SmallTest
    public void testAddOneTabWithinAndOneOutside15MinutesRange() {
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);

        createTabsAndUpdateTabModel(2);

        // Tab_0: Outside 15 minutes range.
        CriticalPersistedTabData.from(mMockTabList.get(0))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS);

        // 15 minutes passes...
        mQuickDeleteTabsFilter.setCurrentTimeForTesting(INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        // Tab_1: Within 15 minutes range.
        CriticalPersistedTabData.from(mMockTabList.get(1))
                .setLastNavigationCommittedTimestampMillis(
                        INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        List<Tab> filteredTabs = mQuickDeleteTabsFilter.getListOfTabsToBeClosed();
        assertEquals(1, filteredTabs.size());
        assertEquals(mMockTabList.get(1), filteredTabs.get(0));
    }

    @Test
    @SmallTest
    public void testClosureOfFilteredTabs_ClosesTabsFromTabModel() {
        // Test close tabs behaviour.
        mQuickDeleteTabsFilter = new QuickDeleteTabsFilter(mTabModelMock);
        createTabsAndUpdateTabModel(2);

        // Tab 1
        CriticalPersistedTabData.from(mMockTabList.get(0))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS + 10);
        // Tab 2
        CriticalPersistedTabData.from(mMockTabList.get(1))
                .setLastNavigationCommittedTimestampMillis(INITIAL_TIME_IN_MS + 20);

        // 15 minutes passes...
        mQuickDeleteTabsFilter.setCurrentTimeForTesting(INITIAL_TIME_IN_MS + FIFTEEN_MINUTES_IN_MS);

        // Initiate quick delete tabs closure.
        List<Tab> filteredTabs = mQuickDeleteTabsFilter.getListOfTabsToBeClosed();
        doNothing().when(mTabModelMock).closeMultipleTabs(eq(filteredTabs), /*canUndo=*/eq(false));
        mQuickDeleteTabsFilter.closeTabsFilteredForQuickDelete();
        verify(mTabModelMock).closeMultipleTabs(eq(filteredTabs), /*canUndo=*/eq(false));
    }
}
