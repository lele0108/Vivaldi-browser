// Copyright 2019 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package org.chromium.chrome.browser.omnibox.suggestions.base;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.graphics.Typeface;
import android.text.Spanned;
import android.text.style.BackgroundColorSpan;
import android.text.style.ForegroundColorSpan;
import android.text.style.StyleSpan;
import android.text.style.TextAppearanceSpan;

import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;
import org.robolectric.Robolectric;
import org.robolectric.annotation.Config;

import org.chromium.base.test.BaseRobolectricTestRunner;
import org.chromium.chrome.browser.omnibox.test.R;

/**
 * Tests for {@link SuggestionSpannable}.
 */
@RunWith(BaseRobolectricTestRunner.class)
@Config(manifest = Config.NONE)
public class SuggestionSpannableUnitTest {
    private Activity mActivity;

    @Before
    public void setUp() {
        mActivity = Robolectric.buildActivity(Activity.class).setup().get();
        mActivity.setTheme(R.style.Theme_BrowserUI_DayNight);
    }

    @Test
    public void spannableEquals_basicStringsComparisonOfSamePlainStrings() {
        SuggestionSpannable span = new SuggestionSpannable("test string");
        assertTrue(span.equals(new SuggestionSpannable("test string")));
    }

    @Test
    public void spannableEquals_basicStringsComparisonOfDifferentPlainStrings() {
        final SuggestionSpannable span = new SuggestionSpannable("test string");
        assertFalse(span.equals(new SuggestionSpannable("different test string")));
        assertFalse(span.equals(new SuggestionSpannable("")));
        assertFalse(span.equals("test string"));
        assertFalse(span.equals(null));
    }

    @Test
    public void spannableEquals_basicStringsComparisonOfSameRichStrings() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertTrue(c1.equals(c2));
    }

    @Test
    public void spannableEquals_differentSpanType() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_differentTypeface() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.ITALIC), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_differentSpanRangeStart() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 5, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_differentSpanRangeEnd() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 4, 6, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_missingAttributesInSource() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 7, 8, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_missingAttributesInOther() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c1.setSpan(new StyleSpan(Typeface.BOLD), 7, 8, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 4, 7, Spanned.SPAN_EXCLUSIVE_INCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_testReflexivity() {
        // Some android versions are susceptible to reordering bug:
        // internal getSpan() method can rearrange returned items, while direct access to
        // mSpans structure lists them in order of appearance.
        // See http://b2/73359036
        SuggestionSpannable c1 = new SuggestionSpannable("test string");

        Object span1 = new Object();
        c1.setSpan(span1, 1, 5, 0);

        Object span2 = new Object();
        c1.setSpan(span2, 0, 5, Spanned.SPAN_PRIORITY);

        SuggestionSpannable c2 = c1;

        assertTrue(c1.equals(c2));
        assertTrue(c2.equals(c1));
    }

    @Test
    public void spannableEquals_worksWithSpansNotOverridingEquals() {
        // Some span styles don't directly override .equals.
        // We don't really care much about the details of these individual spans as long as
        // we only use a limited subset of them that we know well. We care however that our
        // equals call does not give us false negatives.
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        ForegroundColorSpan s1 = new ForegroundColorSpan(0);
        ForegroundColorSpan s2 = new ForegroundColorSpan(0);

        c1.setSpan(s1, 0, 5, 0);
        c2.setSpan(s2, 0, 5, 0);

        assertTrue(c1.equals(c2));
        assertTrue(c2.equals(c1));
    }

    @Test
    public void spannableEquals_differentSpansAtSame() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new BackgroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new ForegroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_backgroundColorSpansSame() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new BackgroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new BackgroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertTrue(c1.equals(c2));
    }

    @Test
    public void spannableEquals_backgroundColorSpansDifferent() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new BackgroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new BackgroundColorSpan(1), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_foregroundColorSpansSame() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new ForegroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new ForegroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertTrue(c1.equals(c2));
    }

    @Test
    public void spannableEquals_foregroundColorSpansDifferent() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new ForegroundColorSpan(0), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new ForegroundColorSpan(1), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_styleSpansSame() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertTrue(c1.equals(c2));
    }

    @Test
    public void spannableEquals_styleSpansDifferent() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new StyleSpan(Typeface.BOLD), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new StyleSpan(Typeface.BOLD_ITALIC), 3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertFalse(c1.equals(c2));
    }

    @Test
    public void spannableEquals_textAppearanceSpansSame() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new TextAppearanceSpan(mActivity, R.style.TextAppearance_TextLarge_Primary), 3,
                5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new TextAppearanceSpan(mActivity, R.style.TextAppearance_TextLarge_Primary), 3,
                5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertTrue(c1.equals(c2));
    }

    @Test
    public void spannableEquals_textAppearanceSpansDifferent() {
        SuggestionSpannable c1 = new SuggestionSpannable("test string");
        SuggestionSpannable c2 = new SuggestionSpannable("test string");

        c1.setSpan(new TextAppearanceSpan(mActivity, R.style.TextAppearance_TextSmall_Disabled), 3,
                5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);
        c2.setSpan(new TextAppearanceSpan(mActivity, R.style.TextAppearance_TextMedium_Secondary),
                3, 5, Spanned.SPAN_INCLUSIVE_EXCLUSIVE);

        assertFalse(c1.equals(c2));
    }
}
