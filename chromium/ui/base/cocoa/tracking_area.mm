// Copyright 2011 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ui/base/cocoa/tracking_area.h"

#include "base/check.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

// NSTrackingArea does not retain its |owner| so CrTrackingArea wraps the real
// owner in this proxy, which can stop forwarding messages to the owner when
// it is no longer |alive_|.
@interface CrTrackingAreaOwnerProxy : NSObject {
  // Whether or not the owner is "alive" and should forward calls to the real
  // owner object.
  BOOL _alive;

  // The real object for which this is a proxy. Weak.
  id __weak _owner;

  // The Class of |owner_|. When the actual object is no longer alive (and could
  // be zombie), this allows for introspection.
  Class __strong _ownerClass;
}
@property(nonatomic, assign) BOOL alive;
- (instancetype)initWithOwner:(id)owner;
@end

@implementation CrTrackingAreaOwnerProxy

@synthesize alive = _alive;

- (instancetype)initWithOwner:(id)owner {
  if ((self = [super init])) {
    _alive = YES;
    _owner = owner;
    _ownerClass = [owner class];
  }
  return self;
}

- (void)forwardInvocation:(NSInvocation*)invocation {
  if (!_alive)
    return;
  [invocation invokeWithTarget:_owner];
}

- (NSMethodSignature*)methodSignatureForSelector:(SEL)sel {
  // This can be called if |owner_| is not |alive_|, so use the Class to
  // generate the signature. |-forwardInvocation:| will block the actual call.
  return [_ownerClass instanceMethodSignatureForSelector:sel];
}

- (BOOL)respondsToSelector:(SEL)aSelector {
  return [_ownerClass instancesRespondToSelector:aSelector];
}

@end

////////////////////////////////////////////////////////////////////////////////

@implementation CrTrackingArea {
  CrTrackingAreaOwnerProxy* __strong _ownerProxy;
}

- (instancetype)initWithRect:(NSRect)rect
           options:(NSTrackingAreaOptions)options
             owner:(id)owner
          userInfo:(NSDictionary*)userInfo{
  CrTrackingAreaOwnerProxy* ownerProxy =
      [[CrTrackingAreaOwnerProxy alloc] initWithOwner:owner];
  if ((self = [super initWithRect:rect
                          options:options
                            owner:ownerProxy
                         userInfo:userInfo])) {
    _ownerProxy = ownerProxy;
  }
  return self;
}

- (void)dealloc {
  [self clearOwner];
  [NSNotificationCenter.defaultCenter removeObserver:self];
}

- (void)clearOwner {
  [_ownerProxy setAlive:NO];
}

@end

// Scoper //////////////////////////////////////////////////////////////////////

namespace ui {

ScopedCrTrackingArea::ScopedCrTrackingArea(CrTrackingArea* tracking_area)
    : tracking_area_(tracking_area) {}

ScopedCrTrackingArea::~ScopedCrTrackingArea() {
  [tracking_area_ clearOwner];
}

void ScopedCrTrackingArea::reset(CrTrackingArea* tracking_area) {
  tracking_area_.reset(tracking_area);
}

CrTrackingArea* ScopedCrTrackingArea::get() const {
  return tracking_area_.get();
}

}  // namespace ui
