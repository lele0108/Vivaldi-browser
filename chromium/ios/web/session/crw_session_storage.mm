// Copyright 2015 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#import "ios/web/public/session/crw_session_storage.h"

#import "base/mac/foundation_util.h"
#import "base/memory/ptr_util.h"
#import "base/metrics/histogram_functions.h"
#import "base/strings/utf_string_conversions.h"
#import "base/time/time.h"
#import "ios/web/common/features.h"
#import "ios/web/navigation/nscoder_util.h"
#import "ios/web/public/session/crw_navigation_item_storage.h"
#import "ios/web/public/session/crw_session_certificate_policy_cache_storage.h"
#import "ios/web/public/session/crw_session_user_data.h"
#import "ios/web/public/session/proto/metadata.pb.h"
#import "ios/web/public/session/proto/navigation.pb.h"
#import "ios/web/public/session/proto/proto_util.h"
#import "ios/web/public/session/proto/storage.pb.h"

#if !defined(__has_feature) || !__has_feature(objc_arc)
#error "This file requires ARC support."
#endif

namespace {
// Serialization keys used in NSCoding functions.
NSString* const kCertificatePolicyCacheStorageKey =
    @"certificatePolicyCacheStorage";
NSString* const kCertificatePolicyCacheStorageDeprecatedKey =
    @"certificatePolicyManager";
NSString* const kItemStoragesKey = @"entries";
NSString* const kHasOpenerKey = @"openedByDOM";
NSString* const kLastCommittedItemIndexKey = @"lastCommittedItemIndex";
NSString* const kUserAgentKey = @"userAgentKey";
NSString* const kStableIdentifierKey = @"stableIdentifier";
NSString* const kUniqueIdentifierKey = @"uniqueIdentifier";
NSString* const kSerializedUserDataKey = @"serializedUserData";
NSString* const kLastActiveTimeKey = @"lastActiveTime";
NSString* const kCreationTimeKey = @"creationTime";

// Deprecated, used for backward compatibility.
// TODO(crbug.com/1278308): Remove this key.
NSString* const kLastCommittedItemIndexDeprecatedKey =
    @"currentNavigationIndex";

// Deprecated, used for backward compatibility for reading the stable
// identifier from the serializable user data as it was stored by the
// external tab helper.
// TODO(crbug.com/1278308): Remove this key.
NSString* const kTabIdKey = @"TabId";
}

@implementation CRWSessionStorage {
  // The unique identifier, stored as the underlying type since SessionID
  // has not public default constructor, thus cannot be an ivar/property.
  SessionID::id_type _uniqueIdentifier;
}

- (instancetype)initWithProto:(const web::proto::WebStateStorage&)storage {
  if ((self = [super init])) {
    _hasOpener = storage.has_opener();
    _userAgentType = web::UserAgentTypeFromProto(storage.user_agent());
    _certPolicyCacheStorage = [[CRWSessionCertificatePolicyCacheStorage alloc]
        initWithProto:storage.certs_cache()];

    const web::proto::NavigationStorage& navigationStorage =
        storage.navigation();
    _lastCommittedItemIndex = navigationStorage.last_committed_item_index();
    NSMutableArray<CRWNavigationItemStorage*>* itemStorages =
        [[NSMutableArray alloc]
            initWithCapacity:navigationStorage.items_size()];
    for (const web::proto::NavigationItemStorage& itemStorage :
         navigationStorage.items()) {
      [itemStorages addObject:[[CRWNavigationItemStorage alloc]
                                  initWithProto:itemStorage]];
    }
    _itemStorages = [itemStorages copy];

    const web::proto::WebStateMetadataStorage& metadataStorage =
        storage.metadata();
    _creationTime = web::TimeFromProto(metadataStorage.creation_time());
    _lastActiveTime = web::TimeFromProto(metadataStorage.last_active_time());
  }
  return self;
}

- (void)serializeToProto:(web::proto::WebStateStorage&)storage {
  storage.set_has_opener(_hasOpener);
  storage.set_user_agent(web::UserAgentTypeToProto(_userAgentType));
  [_certPolicyCacheStorage serializeToProto:*storage.mutable_certs_cache()];

  web::proto::NavigationStorage* navigationStorage =
      storage.mutable_navigation();
  navigationStorage->set_last_committed_item_index(_lastCommittedItemIndex);
  for (CRWNavigationItemStorage* itemStorage in _itemStorages) {
    [itemStorage serializeToProto:*navigationStorage->add_items()];
  }

  web::proto::WebStateMetadataStorage* metadataStorage =
      storage.mutable_metadata();
  web::SerializeTimeToProto(_creationTime,
                            *metadataStorage->mutable_creation_time());
  web::SerializeTimeToProto(_lastActiveTime,
                            *metadataStorage->mutable_last_active_time());
  metadataStorage->set_navigation_item_count(_itemStorages.count);

  if (_lastCommittedItemIndex >= 0) {
    NSUInteger const activePageIndex =
        static_cast<NSUInteger>(_lastCommittedItemIndex);
    if (activePageIndex < _itemStorages.count) {
      CRWNavigationItemStorage* const activePageItem =
          _itemStorages[activePageIndex];
      web::proto::PageMetadataStorage* pageMetadataStorage =
          metadataStorage->mutable_active_page();
      pageMetadataStorage->set_page_title(
          base::UTF16ToUTF8(activePageItem.title));
      pageMetadataStorage->set_page_url(activePageItem.URL.spec());
    }
  }
}

#pragma mark - NSCoding

- (instancetype)initWithCoder:(nonnull NSCoder*)decoder {
  self = [super init];
  if (self) {
    _hasOpener = [decoder decodeBoolForKey:kHasOpenerKey];

    if ([decoder containsValueForKey:kLastCommittedItemIndexKey]) {
      _lastCommittedItemIndex =
          [decoder decodeIntForKey:kLastCommittedItemIndexKey];
    } else {
      // Backward compatibility.
      _lastCommittedItemIndex =
          [decoder decodeIntForKey:kLastCommittedItemIndexDeprecatedKey];
    }

    _itemStorages = [[NSMutableArray alloc]
        initWithArray:[decoder decodeObjectForKey:kItemStoragesKey]];
    // Prior to M34, 0 was used as "no index" instead of -1; adjust for that.
    if (!_itemStorages.count)
      _lastCommittedItemIndex = -1;
    _certPolicyCacheStorage =
        [decoder decodeObjectForKey:kCertificatePolicyCacheStorageKey];
    if (!_certPolicyCacheStorage) {
      // If the cert policy cache was not found, attempt to decode using the
      // deprecated serialization key.
      // TODO(crbug.com/1278308): Remove this deprecated key once we remove
      // support for legacy class conversions.
      _certPolicyCacheStorage = [decoder
          decodeObjectForKey:kCertificatePolicyCacheStorageDeprecatedKey];
    }

    id<NSCoding, NSObject> userData =
        [decoder decodeObjectForKey:kSerializedUserDataKey];
    if ([userData isKindOfClass:[CRWSessionUserData class]]) {
      _userData = base::mac::ObjCCastStrict<CRWSessionUserData>(userData);
    } else if ([userData isKindOfClass:[NSDictionary class]]) {
      // Before M99, the user data was serialized by a C++ class that did
      // serialize a NSDictionary<NSString*, id<NSCoding>>* directly.
      // TODO(crbug.com/1278308): Remove this deprecated logic when we remove
      // support for loading legacy sessions.
      NSDictionary<NSString*, id<NSCoding>>* dictionary =
          base::mac::ObjCCastStrict<NSDictionary>(userData);

      _userData = [[CRWSessionUserData alloc] init];
      for (NSString* key in dictionary) {
        [_userData setObject:dictionary[key] forKey:key];
      }
    }

    if ([decoder containsValueForKey:kUserAgentKey]) {
      std::string userAgentDescription =
          web::nscoder_util::DecodeString(decoder, kUserAgentKey);
      _userAgentType =
          web::GetUserAgentTypeWithDescription(userAgentDescription);
    } else {
      // Prior to M85, the UserAgent wasn't stored.
      // TODO(crbug.com/1278308): Remove this deprecated logic when we
      // remove support for loading legacy sessions.
      _userAgentType = web::UserAgentType::AUTOMATIC;
    }

    _stableIdentifier = [decoder decodeObjectForKey:kStableIdentifierKey];
    if (!_stableIdentifier.length) {
      // Before M99, the stable identifier was managed by a tab helper and
      // saved as part of the serializable user data. To support migration
      // of pre M99 session, read the data from there if not found.

      // If "TabId" is set, clear it and initialise the `stableIdentifier`
      // from it (if it is a NSString and non empty, otherwise a new value
      // will be created below).
      id<NSCoding> tabIdValue = [_userData objectForKey:kTabIdKey];
      if (tabIdValue) {
        [_userData removeObjectForKey:kTabIdKey];

        // If the value is not an NSString or is empty, a random identifier
        // will be generated below.
        _stableIdentifier = base::mac::ObjCCast<NSString>(tabIdValue);
      }
    }

    // If no stable identifier was read, generate a new one (this simplify
    // WebState session restoration code as it can assume that the property
    // is never nil).
    if (!_stableIdentifier.length) {
      _stableIdentifier = [[NSUUID UUID] UUIDString];
    }

    // If no unique identifier was read, or it was invalid, generate a
    // new one.
    static_assert(sizeof(_uniqueIdentifier) == sizeof(int32_t));
    _uniqueIdentifier = [decoder decodeInt32ForKey:kUniqueIdentifierKey];
    if (!SessionID::IsValidValue(_uniqueIdentifier)) {
      _uniqueIdentifier = SessionID::NewUnique().id();
    }

    // Force conversion to NSString if `_stableIdentifier` happens to be a
    // NSMutableString (to prevent this value from being mutated).
    _stableIdentifier = [_stableIdentifier copy];
    DCHECK(_stableIdentifier.length);

    if ([decoder containsValueForKey:kLastActiveTimeKey]) {
      _lastActiveTime = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds([decoder decodeInt64ForKey:kLastActiveTimeKey]));
    }

    if ([decoder containsValueForKey:kCreationTimeKey]) {
      _creationTime = base::Time::FromDeltaSinceWindowsEpoch(
          base::Microseconds([decoder decodeInt64ForKey:kCreationTimeKey]));
    }
  }
  return self;
}

- (void)encodeWithCoder:(NSCoder*)coder {
  [coder encodeBool:self.hasOpener forKey:kHasOpenerKey];
  [coder encodeInt:self.lastCommittedItemIndex
            forKey:kLastCommittedItemIndexKey];
  [coder encodeObject:self.itemStorages forKey:kItemStoragesKey];
  size_t previous_cert_policy_bytes = web::GetCertPolicyBytesEncoded();
  [coder encodeObject:self.certPolicyCacheStorage
               forKey:kCertificatePolicyCacheStorageKey];
  base::UmaHistogramCounts100000(
      "Session.WebStates.SerializedCertPolicyCacheSize",
      web::GetCertPolicyBytesEncoded() - previous_cert_policy_bytes / 1024);

  if (_userData) {
    [coder encodeObject:_userData forKey:kSerializedUserDataKey];
  }

  web::UserAgentType userAgentType = _userAgentType;
  web::nscoder_util::EncodeString(
      coder, kUserAgentKey, web::GetUserAgentTypeDescription(userAgentType));
  [coder encodeObject:_stableIdentifier forKey:kStableIdentifierKey];

  if (!_lastActiveTime.is_null()) {
    [coder
        encodeInt64:_lastActiveTime.ToDeltaSinceWindowsEpoch().InMicroseconds()
             forKey:kLastActiveTimeKey];
  }

  if (!_creationTime.is_null()) {
    [coder encodeInt64:_creationTime.ToDeltaSinceWindowsEpoch().InMicroseconds()
                forKey:kCreationTimeKey];
  }

  if (SessionID::IsValidValue(_uniqueIdentifier)) {
    static_assert(sizeof(_uniqueIdentifier) == sizeof(int32_t));
    [coder encodeInt32:_uniqueIdentifier forKey:kUniqueIdentifierKey];
  }
}

#pragma mark - Properties

- (SessionID)uniqueIdentifier {
  DCHECK(SessionID::IsValidValue(_uniqueIdentifier));
  return SessionID::FromSerializedValue(_uniqueIdentifier);
}

- (void)setUniqueIdentifier:(SessionID)uniqueIdentifier {
  DCHECK(uniqueIdentifier.is_valid());
  _uniqueIdentifier = uniqueIdentifier.id();
}

@end
