// Copyright 2012 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_POPUP_VIEW_VIEWS_H_
#define CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_POPUP_VIEW_VIEWS_H_

#include <stddef.h>

#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "components/omnibox/browser/omnibox_popup_selection.h"
#include "components/omnibox/browser/omnibox_popup_view.h"
#include "components/prefs/pref_change_registrar.h"
#include "ui/accessibility/ax_node_data.h"
#include "ui/base/metadata/metadata_header_macros.h"
#include "ui/base/window_open_disposition.h"
#include "ui/gfx/font_list.h"
#include "ui/gfx/image/image.h"
#include "ui/views/view.h"
#include "ui/views/widget/widget_observer.h"

struct AutocompleteMatch;
class LocationBarView;
class OmniboxEditModel;
class OmniboxResultView;
class OmniboxViewViews;

// A view representing the contents of the autocomplete popup.
class OmniboxPopupViewViews : public views::View,
                              public OmniboxPopupView,
                              public views::WidgetObserver {
 public:
  METADATA_HEADER(OmniboxPopupViewViews);
  OmniboxPopupViewViews(OmniboxViewViews* omnibox_view,
                        OmniboxEditModel* edit_model,
                        LocationBarView* location_bar_view);
  explicit OmniboxPopupViewViews(const OmniboxPopupViewViews&) = delete;
  OmniboxPopupViewViews& operator=(const OmniboxPopupViewViews&) = delete;
  ~OmniboxPopupViewViews() override;

  // Returns the icon that should be displayed next to |match|. If the icon is
  // available as a vector icon, it will be |vector_icon_color|.
  gfx::Image GetMatchIcon(const AutocompleteMatch& match,
                          SkColor vector_icon_color) const;

  // Sets the line specified by |index| as selected and, if |index| is
  // different than the previous index, sets the line state to NORMAL.
  virtual void SetSelectedIndex(size_t index);

  // Returns the selected line.
  // Note: This and `SetSelectedIndex` above are used by property
  // metadata and must follow the metadata conventions.
  virtual size_t GetSelectedIndex() const;

  // Returns current popup selection (includes line index).
  virtual OmniboxPopupSelection GetSelection() const;

  // OmniboxPopupView:
  bool IsOpen() const override;
  void InvalidateLine(size_t line) override;
  void OnSelectionChanged(OmniboxPopupSelection old_selection,
                          OmniboxPopupSelection new_selection) override;
  void UpdatePopupAppearance() override;
  void ProvideButtonFocusHint(size_t line) override;
  void OnMatchIconUpdated(size_t match_index) override;
  void OnDragCanceled() override;
  void GetPopupAccessibleNodeData(ui::AXNodeData* node_data) override;
  void AddPopupAccessibleNodeData(ui::AXNodeData* node_data) override;
  std::u16string GetAccessibleButtonTextForResult(size_t line) override;

  // views::View:
  bool OnMouseDragged(const ui::MouseEvent& event) override;
  void OnGestureEvent(ui::GestureEvent* event) override;
  void GetAccessibleNodeData(ui::AXNodeData* node_data) override;

  // views::WidgetObserver:
  void OnWidgetBoundsChanged(views::Widget* widget,
                             const gfx::Rect& new_bounds) override;

  void FireAXEventsForNewActiveDescendant(View* descendant_view);

 protected:
  FRIEND_TEST_ALL_PREFIXES(OmniboxPopupViewViewsTest, ClickOmnibox);
  friend class OmniboxPopupViewViewsTest;
  friend class OmniboxSuggestionButtonRowBrowserTest;
  class AutocompletePopupWidget;

  // Add and update any child views; called by `UpdatePopupAppearance`.
  virtual void UpdateChildViews();

  // Called by `UpdatePopupAppearance` after popup is created.
  virtual void OnPopupCreated();

  // Returns the target popup bounds in screen coordinates based on the bounds
  // of |location_bar_view_|.
  virtual gfx::Rect GetTargetBounds() const;

  // Gets the OmniboxResultView for match |i|.
  OmniboxResultView* result_view_at(size_t i);

  // Returns true if the model has a match at the specified index.
  bool HasMatchAt(size_t index) const;

  // Returns the match at the specified index within the model.
  const AutocompleteMatch& GetMatchAtIndex(size_t index) const;

  // Find the index of the match under the given |point|, specified in window
  // coordinates. Returns OmniboxPopupSelection::kNoMatch if there isn't a match
  // at the specified point.
  size_t GetIndexForPoint(const gfx::Point& point);

  // Update which result views are visible when the group visibility changes.
  void OnSuggestionGroupVisibilityUpdate();

  // Gets the pref service for this view. May return nullptr in tests.
  PrefService* GetPrefService() const;

  LocationBarView* location_bar_view() const { return location_bar_view_; }

 private:
  // The popup that contains this view.  We create this, but it deletes itself
  // when its window is destroyed.  This is a WeakPtr because it's possible for
  // the OS to destroy the window and thus delete this object before we're
  // deleted, or without our knowledge.
  base::WeakPtr<AutocompletePopupWidget> popup_;

  // The edit view that invokes us.
  raw_ptr<OmniboxViewViews> omnibox_view_;

  // The location bar view that owns |omnibox_view_|. May be nullptr in tests.
  raw_ptr<LocationBarView> location_bar_view_;

  // A pref change registrar for toggling result view visibility.
  PrefChangeRegistrar pref_change_registrar_;

  raw_ptr<OmniboxEditModel> edit_model_;
};

#endif  // CHROME_BROWSER_UI_VIEWS_OMNIBOX_OMNIBOX_POPUP_VIEW_VIEWS_H_
