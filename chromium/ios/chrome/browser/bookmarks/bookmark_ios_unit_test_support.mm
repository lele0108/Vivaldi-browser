// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/bookmarks/bookmark_ios_unit_test_support.h"
#import <memory>

#import "base/strings/sys_string_conversions.h"
#import "components/bookmarks/browser/bookmark_model.h"
#import "components/bookmarks/common/bookmark_features.h"
#import "components/bookmarks/common/bookmark_metrics.h"
#import "components/bookmarks/test/bookmark_test_helpers.h"
#import "ios/chrome/browser/bookmarks/account_bookmark_model_factory.h"
#import "ios/chrome/browser/bookmarks/local_or_syncable_bookmark_model_factory.h"
#import "ios/chrome/browser/bookmarks/managed_bookmark_service_factory.h"
#import "ios/chrome/browser/shared/model/browser/test/test_browser.h"
#import "ios/chrome/browser/shared/model/browser_state/test_chrome_browser_state.h"
#import "ios/chrome/browser/signin/authentication_service_factory.h"
#import "ios/chrome/browser/signin/fake_authentication_service_delegate.h"
#import "url/gurl.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

using bookmarks::BookmarkNode;

BookmarkIOSUnitTestSupport::BookmarkIOSUnitTestSupport() = default;
BookmarkIOSUnitTestSupport::~BookmarkIOSUnitTestSupport() = default;

void BookmarkIOSUnitTestSupport::SetUp() {
  // Get a BookmarkModel from the test ChromeBrowserState.
  TestChromeBrowserState::Builder test_cbs_builder;
  test_cbs_builder.AddTestingFactory(
      AuthenticationServiceFactory::GetInstance(),
      AuthenticationServiceFactory::GetDefaultFactory());
  test_cbs_builder.AddTestingFactory(
      ios::LocalOrSyncableBookmarkModelFactory::GetInstance(),
      ios::LocalOrSyncableBookmarkModelFactory::GetDefaultFactory());
  test_cbs_builder.AddTestingFactory(
      ios::AccountBookmarkModelFactory::GetInstance(),
      ios::AccountBookmarkModelFactory::GetDefaultFactory());
  test_cbs_builder.AddTestingFactory(
      ManagedBookmarkServiceFactory::GetInstance(),
      ManagedBookmarkServiceFactory::GetDefaultFactory());

  chrome_browser_state_ = test_cbs_builder.Build();
  AuthenticationServiceFactory::CreateAndInitializeForBrowserState(
      chrome_browser_state_.get(),
      std::make_unique<FakeAuthenticationServiceDelegate>());

  local_or_syncable_bookmark_model_ =
      ios::LocalOrSyncableBookmarkModelFactory::GetForBrowserState(
          chrome_browser_state_.get());
  bookmarks::test::WaitForBookmarkModelToLoad(
      local_or_syncable_bookmark_model_);
  account_bookmark_model_ =
      ios::AccountBookmarkModelFactory::GetForBrowserState(
          chrome_browser_state_.get());
  if (account_bookmark_model_) {
    bookmarks::test::WaitForBookmarkModelToLoad(account_bookmark_model_);
  }
  browser_ = std::make_unique<TestBrowser>(chrome_browser_state_.get());
}

const BookmarkNode* BookmarkIOSUnitTestSupport::AddBookmark(
    const BookmarkNode* parent,
    const std::u16string& title) {
  GURL url(u"http://example.com/bookmark" + title);
  return AddBookmark(parent, title, url);
}

const BookmarkNode* BookmarkIOSUnitTestSupport::AddBookmark(
    const BookmarkNode* parent,
    const std::u16string& title,
    const GURL& url) {
  bookmarks::BookmarkModel* model = GetBookmarkModelForNode(parent);
  return model->AddURL(parent, parent->children().size(), title, url);
}

const BookmarkNode* BookmarkIOSUnitTestSupport::AddFolder(
    const BookmarkNode* parent,
    const std::u16string& title) {
  bookmarks::BookmarkModel* model = GetBookmarkModelForNode(parent);
  return model->AddFolder(parent, parent->children().size(), title);
}

void BookmarkIOSUnitTestSupport::ChangeTitle(const std::u16string& title,
                                             const BookmarkNode* node) {
  bookmarks::BookmarkModel* model = GetBookmarkModelForNode(node);
  model->SetTitle(node, title, bookmarks::metrics::BookmarkEditSource::kUser);
}

bookmarks::BookmarkModel* BookmarkIOSUnitTestSupport::GetBookmarkModelForNode(
    const BookmarkNode* node) {
  if (node->HasAncestor(local_or_syncable_bookmark_model_->root_node())) {
    return local_or_syncable_bookmark_model_;
  }
  DCHECK(account_bookmark_model_ &&
         node->HasAncestor(account_bookmark_model_->root_node()));
  return account_bookmark_model_;
}
