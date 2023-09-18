// Copyright 2022 Vivaldi Technologies AS. All rights reserved.

#ifndef IOS_NOTES_CELLS_NOTE_HOME_NODE_ITEM_H_
#define IOS_NOTES_CELLS_NOTE_HOME_NODE_ITEM_H_

#import "ios/chrome/browser/shared/ui/table_view/cells/table_view_item.h"

namespace vivaldi {
class NoteNode;
}

// NoteHomeNodeItem provides data for a table view row that displays a
// single note in the note list
@interface NoteHomeNodeItem : TableViewItem

// The NoteNode that backs this item.
@property(nonatomic, readwrite, assign)
    const vivaldi::NoteNode* noteNode;

- (instancetype)initWithType:(NSInteger)type
                noteNode:(const vivaldi::NoteNode*)node
    NS_DESIGNATED_INITIALIZER;
- (instancetype)initWithType:(NSInteger)type NS_UNAVAILABLE;

@end

#endif  // IOS_NOTES_CELLS_NOTE_HOME_NODE_ITEM_H_
