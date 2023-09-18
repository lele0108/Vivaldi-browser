// Copyright (c) 2015 Vivaldi Technologies AS. All rights reserved

#ifndef SYNC_VIVALDI_SYNC_CLIENT_H_
#define SYNC_VIVALDI_SYNC_CLIENT_H_

#include <memory>

#include "chrome/browser/sync/chrome_sync_client.h"

namespace vivaldi {
class VivaldiInvalidationService;
class NotesModel;

class VivaldiSyncClient : public browser_sync::ChromeSyncClient {
 public:
  explicit VivaldiSyncClient(Profile*);
  ~VivaldiSyncClient() override;

  invalidation::InvalidationService* GetInvalidationService() override;

 private:
  const raw_ptr<Profile> profile_;
};
}  // namespace vivaldi

#endif  // SYNC_VIVALDI_SYNC_CLIENT_H_
