// Copyright 2022 Vivaldi Technologies. All rights reserved.

#import "ios/ui/bookmarks_editor/vivaldi_bookmarks_constants.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

#pragma mark - ICONS
// Image name for the bookmark folder icon.
NSString* vBookmarksFolderIcon = @"vivaldi_bookmarks_folder";
// Image name for the speed dial folder icon.
NSString* vSpeedDialFolderIcon = @"vivaldi_speed_dial_folder";
// Image name for the folder selection chevron.
NSString* vBookmarkFolderSelectionChevron =
  @"vivaldi_bookmark_folder_selection_chevron";
// Image name for the folder selection checkmark.
NSString* vBookmarkFolderSelectionCheckmark =
  @"vivaldi_bookmark_folder_selection_checkmark";
// Image name for add folder
NSString* vBookmarkAddFolder =
  @"vivaldi_bookmark_add_new_folder";

#pragma mark - COLOR
// Underline color for bookmark text field underline
NSString* vBookmarkTextFieldUnderlineColor =
  @"vivaldi_bookmark_text_field_underline_color";

#pragma mark - SIZE
// Corner radius for the bookmark body containr view
CGFloat vBookmarkBodyCornerRadius = 6.0;
// Bookmark editor text field height
CGFloat vBookmarkTextViewHeight = 44.0;
// Bookmark folder selection header view height
CGFloat vBookmarkFolderSelectionHeaderViewHeight = 120.0;

#pragma mark - OTHERS
// Maximum number of entries to fetch when searching on bookmarks folder page.
const int vMaxBookmarkFolderSearchResults = 50;
