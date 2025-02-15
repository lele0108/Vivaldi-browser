// Copyright 2014 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/chrome/browser/ui/bookmarks/folder_chooser/bookmarks_folder_chooser_view_controller.h"

#import <memory>
#import <vector>

#import "base/check.h"
#import "base/containers/contains.h"
#import "base/metrics/user_metrics.h"
#import "base/metrics/user_metrics_action.h"
#import "base/notreached.h"
#import "base/strings/sys_string_conversions.h"
#import "components/bookmarks/browser/bookmark_model.h"
#import "components/bookmarks/common/bookmark_features.h"
#import "ios/chrome/browser/bookmarks/bookmark_model_bridge_observer.h"
#import "ios/chrome/browser/shared/ui/symbols/chrome_icon.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_text_header_footer_item.h"
#import "ios/chrome/browser/shared/ui/table_view/table_view_utils.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_ui_constants.h"
#import "ios/chrome/browser/ui/bookmarks/bookmark_utils_ios.h"
#import "ios/chrome/browser/ui/bookmarks/cells/table_view_bookmarks_folder_item.h"
#import "ios/chrome/browser/ui/bookmarks/folder_chooser/bookmarks_folder_chooser_mutator.h"
#import "ios/chrome/browser/ui/bookmarks/folder_chooser/bookmarks_folder_chooser_view_controller_presentation_delegate.h"
#import "ios/chrome/common/ui/colors/semantic_color_names.h"
#import "ios/chrome/grit/ios_strings.h"
#import "ui/base/l10n/l10n_util_mac.h"

// Vivaldi
#import "app/vivaldi_apptools.h"
#import "base/mac/foundation_util.h"
#import "components/bookmarks/browser/bookmark_utils.h"
#import "components/bookmarks/browser/bookmark_utils.h"
#import "components/bookmarks/vivaldi_bookmark_kit.h"
#import "components/strings/grit/components_strings.h"
#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_text_item.h"
#import "ios/ui/bookmarks_editor/vivaldi_bookmark_folder_selection_header_view.h"
#import "ios/ui/bookmarks_editor/vivaldi_bookmarks_constants.h"
#import "ios/ui/helpers/vivaldi_uiview_layout_helper.h"
#import "ios/ui/ntp/vivaldi_speed_dial_item.h"

using vivaldi_bookmark_kit::GetSpeeddial;
using vivaldi::IsVivaldiRunning;
// End Vivaldi

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {

// The estimated height of every folder cell.
const CGFloat kEstimatedFolderCellHeight = 48.0;

// Height of section headers/footers.
#if !defined(VIVALDI_BUILD)
const CGFloat kSectionHeaderHeight = 8.0;
#endif

typedef NS_ENUM(NSInteger, SectionIdentifier) {
  SectionIdentifierProfileBookmarks = kSectionIdentifierEnumZero,
  SectionIdentifierAccountBookmarks,

  // Vivaldi
  BookmarksHomeSectionIdentifierMessages
  // End Vivaldi

};

typedef NS_ENUM(NSInteger, ItemType) {
  ItemTypeHeader = kItemTypeEnumZero,
  ItemTypeCreateNewFolder,
  ItemTypeBookmarkFolder,
  BookmarkHomeItemTypeMessage // Vivaldi
};

}  // namespace

using bookmarks::BookmarkNode;

@interface BookmarksFolderChooserViewController () <UITableViewDataSource,
                                                    UITableViewDelegate,
    VivaldiBookmarkFolderSelectionHeaderViewDelegate>

// Vivaldi
@property(nonatomic, weak)
  VivaldiBookmarkFolderSelectionHeaderView* tableHeaderView;
@property(nonatomic, strong) NSString* searchText;
// End Vivaldi

@end

@implementation BookmarksFolderChooserViewController {
  // Should the controller setup Cancel and Done buttons instead of a back
  // button.
  BOOL _allowsCancel;
  // Should the controller setup a new-folder button.
  BOOL _allowsNewFolders;
  // A linear list of folders. This will be populated in `reloadView` when the
  // UI is updated.
  std::vector<const BookmarkNode*> _accountFolderNodes;
  // A linear list of folders. This will be populated in `reloadView` when the
  // UI is updated.
  std::vector<const BookmarkNode*> _profileFolderNodes;
}

// Vivaldi
@synthesize tableHeaderView = _tableHeaderView;
@synthesize searchText = _searchText;
// End Vivaldi

- (instancetype)initWithAllowsCancel:(BOOL)allowsCancel
                    allowsNewFolders:(BOOL)allowsNewFolders {
  UITableViewStyle style = ChromeTableViewStyle();
  self = [super initWithStyle:style];
  if (self) {
    _allowsCancel = allowsCancel;
    _allowsNewFolders = allowsNewFolders;
  }
  return self;
}

#pragma mark - UIViewController

- (void)viewDidLoad {
  [super viewDidLoad];
  [super loadModel];

  self.view.accessibilityIdentifier =
      kBookmarkFolderPickerViewContainerIdentifier;
  self.title = l10n_util::GetNSString(IDS_IOS_BOOKMARK_CHOOSE_GROUP_BUTTON);

  if (_allowsCancel) {
    UIBarButtonItem* cancelItem = [[UIBarButtonItem alloc]
        initWithBarButtonSystemItem:UIBarButtonSystemItemCancel
                             target:self
                             action:@selector(cancel:)];
    cancelItem.accessibilityIdentifier = @"Cancel";
    self.navigationItem.leftBarButtonItem = cancelItem;
  }
  // Configure the table view.
  self.tableView.autoresizingMask =
      UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;

  self.tableView.estimatedRowHeight = kEstimatedFolderCellHeight;
  self.tableView.rowHeight = UITableViewAutomaticDimension;

  // Vivaldi
  [self setUpTableHeaderView];
  [self updateTableHeaderViewState];
  // End Vivaldi

}

- (void)viewWillAppear:(BOOL)animated {
  [super viewWillAppear:animated];
  // Whevener this VC is displayed the bottom toolbar will be hidden.
  self.navigationController.toolbarHidden = YES;

  // Load the model.

  if (IsVivaldiRunning()) {
    [self reloadModelVivaldi];
  } else {
  [self reloadView];
  } // End Vivaldi

}

- (void)didMoveToParentViewController:(UIViewController*)parent {
  [super didMoveToParentViewController:parent];
  if (!parent) {
    [self.delegate bookmarksFolderChooserViewControllerDidDismiss:self];
  }
}

#pragma mark - Accessibility

- (BOOL)accessibilityPerformEscape {
  [self.delegate bookmarksFolderChooserViewControllerDidCancel:self];
  return YES;
}

#pragma mark - UITableViewDelegate

- (void)tableView:(UITableView*)tableView
    didSelectRowAtIndexPath:(NSIndexPath*)indexPath {
  [tableView deselectRowAtIndexPath:indexPath animated:YES];

  size_t folderIndex = indexPath.row;
  NSInteger sectionID =
      [self.tableViewModel sectionIdentifierForSectionIndex:indexPath.section];
  // If new folders are allowed, the first cell on this section should call
  // `showBookmarksFolderEditor`.
  if (_allowsNewFolders) {
    NSInteger itemType = [self.tableViewModel itemTypeForIndexPath:indexPath];
    if (itemType == ItemTypeCreateNewFolder) {
      // Set the 'Mobile Bookmarks' folder of the corresponding section to be
      // the parent folder.
      const BookmarkNode* parentNode = nullptr;
      if (!base::FeatureList::IsEnabled(
              bookmarks::kEnableBookmarksAccountStorage)) {
        parentNode = [_dataSource selectedFolderNode];
      }
      if (!parentNode) {
        // If `parent` (selected folder) is `nullptr`, set the root folder of
        // the corresponding section to be the parent folder.
        parentNode = (sectionID == SectionIdentifierAccountBookmarks)
                         ? [_dataSource.accountDataSource mobileFolderNode]
                         : [_dataSource.profileDataSource mobileFolderNode];
      }
      [self.delegate showBookmarksFolderEditorWithParentFolderNode:parentNode];
      return;
    }
    // If new folders are allowed, we need to offset by 1 to get the right
    // BookmarkNode from folders.
    DCHECK(folderIndex > 0);
    folderIndex--;
  }

  const BookmarkNode* folder;

  if (IsVivaldiRunning()) {
    TableViewBookmarksFolderItem* folderItem =
        base::mac::ObjCCast<TableViewBookmarksFolderItem>(
            [self.tableViewModel itemAtIndexPath:indexPath]);
    folder = folderItem.bookmarkNode;
  } else {
  if (sectionID == SectionIdentifierAccountBookmarks) {
    DCHECK(folderIndex < _accountFolderNodes.size());
    folder = _accountFolderNodes[folderIndex];
  } else {
    DCHECK(folderIndex < _profileFolderNodes.size());
    folder = _profileFolderNodes[folderIndex];
  }
  } // End Vivaldi

  [_mutator setSelectedFolderNode:folder];
  [self delayedNotifyDelegateOfSelection];
}

#pragma mark - BookmarksFolderChooserConsumer

- (void)notifyModelUpdated {
  [self reloadView];
}

#pragma mark - Actions

- (void)done:(id)sender {
  base::RecordAction(
      base::UserMetricsAction("MobileBookmarksFolderChooserDone"));
  [self.delegate
      bookmarksFolderChooserViewController:self
                       didFinishWithFolder:[_dataSource selectedFolderNode]];
}

- (void)cancel:(id)sender {
  base::RecordAction(
      base::UserMetricsAction("MobileBookmarksFolderChooserCanceled"));
  [self.delegate bookmarksFolderChooserViewControllerDidCancel:self];
}

#pragma mark - Private

- (void)reloadView {
  // Delete any existing section.
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:SectionIdentifierAccountBookmarks]) {
    [self.tableViewModel
        removeSectionWithIdentifier:SectionIdentifierAccountBookmarks];
  }
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:SectionIdentifierProfileBookmarks]) {
    [self.tableViewModel
        removeSectionWithIdentifier:SectionIdentifierProfileBookmarks];
  }

  if ([_dataSource shouldShowAccountBookmarks]) {
    _accountFolderNodes = [_dataSource.accountDataSource visibleFolderNodes];
    [self reloadSectionWithIdentifier:SectionIdentifierAccountBookmarks];
  }
  _profileFolderNodes = [_dataSource.profileDataSource visibleFolderNodes];
  [self reloadSectionWithIdentifier:SectionIdentifierProfileBookmarks];
  if ([_dataSource shouldShowAccountBookmarks]) {
    // The headers are only shown if both sections are visible.
    [self.tableViewModel setHeader:[self headerForSectionWithIdentifier:
                                             SectionIdentifierAccountBookmarks]
          forSectionWithIdentifier:SectionIdentifierAccountBookmarks];
    [self.tableViewModel setHeader:[self headerForSectionWithIdentifier:
                                             SectionIdentifierProfileBookmarks]
          forSectionWithIdentifier:SectionIdentifierProfileBookmarks];
  }
  [self.tableView reloadData];
}

- (void)reloadSectionWithIdentifier:(SectionIdentifier)sectionID {
  // Creates Folders Section
  [self.tableViewModel addSectionWithIdentifier:sectionID];

  // Adds default "New Folder" item if needed.
  if (_allowsNewFolders) {
    TableViewBookmarksFolderItem* createFolderItem =
        [[TableViewBookmarksFolderItem alloc]
            initWithType:ItemTypeCreateNewFolder
                   style:BookmarksFolderStyleNewFolder];
    createFolderItem.accessibilityIdentifier =
        (sectionID == SectionIdentifierProfileBookmarks)
            ? kBookmarkCreateNewProfileFolderCellIdentifier
            : kBookmarkCreateNewAccountFolderCellIdentifier;
    createFolderItem.shouldDisplayCloudSlashIcon =
        (sectionID == SectionIdentifierProfileBookmarks) &&
        [_dataSource shouldDisplayCloudIconForProfileBookmarks];
    // Add the "New Folder" Item to the same section as the rest of the folder
    // entries.
    [self.tableViewModel addItem:createFolderItem
         toSectionWithIdentifier:sectionID];
  }

  // Add Folders entries.
  const std::vector<const BookmarkNode*>& folders =
      (sectionID == SectionIdentifierAccountBookmarks) ? _accountFolderNodes
                                                       : _profileFolderNodes;
  const BookmarkNode* rootFolderNode =
      (sectionID == SectionIdentifierAccountBookmarks)
          ? [_dataSource.accountDataSource rootFolderNode]
          : [_dataSource.profileDataSource rootFolderNode];
  for (const BookmarkNode* folderNode : folders) {
    TableViewBookmarksFolderItem* folderItem =
        [[TableViewBookmarksFolderItem alloc]
            initWithType:ItemTypeBookmarkFolder
                   style:BookmarksFolderStyleFolderEntry];
    folderItem.title = bookmark_utils_ios::TitleForBookmarkNode(folderNode);
    folderItem.currentFolder = ([_dataSource selectedFolderNode] == folderNode);
    folderItem.accessibilityIdentifier = folderItem.title;
    folderItem.shouldDisplayCloudSlashIcon =
        (sectionID == SectionIdentifierProfileBookmarks) &&
        [_dataSource shouldDisplayCloudIconForProfileBookmarks];

    // Vivaldi
    folderItem.bookmarkNode = folderNode;
    folderItem.isSpeedDial = GetSpeeddial(folderNode);
    // End Vivaldi

    if (IsVivaldiRunning()) {
      [self computeBookmarksForVivaldi:folderItem
                            folderNode:folderNode
                        rootFolderNode:rootFolderNode
                             sectionID:sectionID];
    } else {
    // Indentation level.
    NSInteger level = 0;
    while (folderNode && folderNode != rootFolderNode) {
      ++level;
      folderNode = folderNode->parent();
    }
    // The root node is not shown as a folder, so top level folders have a
    // level strictly positive.
    DCHECK(level > 0);
    folderItem.indentationLevel = level - 1;
    [self.tableViewModel addItem:folderItem toSectionWithIdentifier:sectionID];
    } // End Vivaldi
  }
}

- (TableViewHeaderFooterItem*)headerForSectionWithIdentifier:
    (SectionIdentifier)sectionID {
  TableViewTextHeaderFooterItem* header =
      [[TableViewTextHeaderFooterItem alloc] initWithType:ItemTypeHeader];

  switch (sectionID) {
    case SectionIdentifierProfileBookmarks:
      header.text =
          l10n_util::GetNSString(IDS_IOS_BOOKMARKS_PROFILE_SECTION_TITLE);
      break;
    case SectionIdentifierAccountBookmarks:
      header.text =
          l10n_util::GetNSString(IDS_IOS_BOOKMARKS_ACCOUNT_SECTION_TITLE);
      break;

      // Vivaldi
    default:
      break;
      // End Vivaldi

  }
  return header;
}

- (void)delayedNotifyDelegateOfSelection {
  self.view.userInteractionEnabled = NO;
  __weak BookmarksFolderChooserViewController* weakSelf = self;
  dispatch_after(
      dispatch_time(DISPATCH_TIME_NOW, (int64_t)(0.3 * NSEC_PER_SEC)),
      dispatch_get_main_queue(), ^{
        BookmarksFolderChooserViewController* strongSelf = weakSelf;
        // Early return if the controller has been deallocated.
        if (!strongSelf) {
          return;
        }
        strongSelf.view.userInteractionEnabled = YES;
        [strongSelf done:nil];
      });
}

#pragma mark - VIVALDI

/// Set up the table header view that contains the search bar and toggle to show
/// only speed dial folders.
- (void)setUpTableHeaderView {
  VivaldiBookmarkFolderSelectionHeaderView* tableHeaderView =
    [VivaldiBookmarkFolderSelectionHeaderView new];
  _tableHeaderView = tableHeaderView;
  tableHeaderView.frame = CGRectMake(0, 0,
                                     self.view.bounds.size.width,
                                     vBookmarkFolderSelectionHeaderViewHeight);
  self.tableView.tableHeaderView = tableHeaderView;
  tableHeaderView.delegate = self;
}

/// This is an extension for chromium 'reloadModel' method to compute bookmarks
/// for Vivaldi respecting Vivaldi pref. Note that the indentation is available only
/// when all folders are visible.
- (void)computeBookmarksForVivaldi:(TableViewBookmarksFolderItem*)folderItem
                        folderNode:(const BookmarkNode*)folderNode
                    rootFolderNode:(const BookmarkNode*)rootFolderNode
                         sectionID:(SectionIdentifier)sectionID {

  // Remove empty message section if available
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:BookmarksHomeSectionIdentifierMessages])
    [self.tableViewModel
        removeSectionWithIdentifier:BookmarksHomeSectionIdentifierMessages];

  // Folder indentation will be available when all folders are visible. When
  // only speed dial folders are visible, list the folders normally.
  if (self.showOnlySDFolders) {
    if (folderItem.isSpeedDial)
      [self.tableViewModel addItem:folderItem
          toSectionWithIdentifier:sectionID];
  } else {
    // Indentation level.
    NSInteger level = 0;
    while (folderNode && folderNode != rootFolderNode) {
      ++level;
      folderNode = folderNode->parent();
    }
    // The root node is not shown as a folder, so top level folders have a
    // level strictly positive.
    DCHECK(level > 0);
    folderItem.indentationLevel = level - 1;
    [self.tableViewModel addItem:folderItem toSectionWithIdentifier:sectionID];
  }
}

/// This method is reponsible for searching through the bookmark model
/// with search keyword and return the result. If there are no result
/// and empty result cell will return. Note that this only returns the folder
/// and respects the setting 'Show only Speed dial folders'
- (void)computeBookmarkTableViewDataMatching:(NSString*)searchText
                  orShowMessageWhenNoResults:(NSString*)noResults {

  // Delete any existing section.
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:SectionIdentifierAccountBookmarks])
    [self.tableViewModel
        removeSectionWithIdentifier:SectionIdentifierAccountBookmarks];
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:SectionIdentifierProfileBookmarks])
    [self.tableViewModel
        removeSectionWithIdentifier:SectionIdentifierProfileBookmarks];
  if ([self.tableViewModel
          hasSectionForSectionIdentifier:BookmarksHomeSectionIdentifierMessages])
    [self.tableViewModel
        removeSectionWithIdentifier:BookmarksHomeSectionIdentifierMessages];

  // Creates Folders and empty result Section
  [self.tableViewModel
      addSectionWithIdentifier:SectionIdentifierProfileBookmarks];
  [self.tableViewModel
      addSectionWithIdentifier:BookmarksHomeSectionIdentifierMessages];

  std::vector<const BookmarkNode*> nodes;
  bookmarks::QueryFields query;
  query.word_phrase_query.reset(new std::u16string);
  *query.word_phrase_query = base::SysNSStringToUTF16(searchText);

  GetBookmarksMatchingProperties([_dataSource profileBookmarkModel],
                                 query,
                                 vMaxBookmarkFolderSearchResults,
                                 &nodes);

  int count = 0;
  for (const BookmarkNode* node : nodes) {
    if (!node->is_folder()) {
      continue;
    }

    if (self.showOnlySDFolders) {
      if (!GetSpeeddial(node)) {
        continue;
      }
    }

    // When search result is visible ignore the folder indentation and
    // show a normal list.
    TableViewBookmarksFolderItem* folderItem =
        [[TableViewBookmarksFolderItem alloc]
            initWithType:ItemTypeBookmarkFolder
                   style:BookmarksFolderStyleFolderEntry];
    folderItem.title = bookmark_utils_ios::TitleForBookmarkNode(node);
    folderItem.currentFolder = ([_dataSource selectedFolderNode] == node);
    folderItem.bookmarkNode = node;
    folderItem.isSpeedDial = GetSpeeddial(node);

    [self.tableViewModel addItem:folderItem
         toSectionWithIdentifier:SectionIdentifierProfileBookmarks];

    count++;
  }

  if (count == 0) {
    TableViewTextItem* item =
      [[TableViewTextItem alloc] initWithType:BookmarkHomeItemTypeMessage];
    item.textAlignment = NSTextAlignmentLeft;
    item.textColor = [UIColor colorNamed:kTextPrimaryColor];
    item.text = noResults;
    [self.tableViewModel addItem:item
         toSectionWithIdentifier:BookmarksHomeSectionIdentifierMessages];
  }
}

/// Updates the UISwitch state from pref.
- (void)updateTableHeaderViewState {
  [_tableHeaderView setShowOnlySpeedDialFolder:self.showOnlySDFolders];
}

- (void)reloadModelVivaldi {
  if ([self.searchText length] == 0) {
    [self reloadView];
  } else {
    NSString* noResults = l10n_util::GetNSString(IDS_HISTORY_NO_SEARCH_RESULTS);
    [self computeBookmarkTableViewDataMatching:self.searchText
                    orShowMessageWhenNoResults:noResults];
    [self.tableView reloadData];
  }
}

#pragma mark - VivaldiBookmarkFolderSelectionHeaderViewDelegate
- (void)didChangeShowOnlySpeedDialFoldersState:(BOOL)show {
  self.showOnlySDFolders = show;
  [self reloadModelVivaldi];
  [_mutator setShowOnlySpeedDialFolder:show];
}

- (void)searchBarTextDidChange:(NSString*)searchText {
  self.searchText = searchText;
  [self reloadModelVivaldi];
}

@end
