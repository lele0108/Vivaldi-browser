// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "services/network/shared_dictionary/shared_dictionary_storage_on_disk.h"

#include "base/functional/callback_helpers.h"
#include "base/logging.h"
#include "base/memory/ref_counted.h"
#include "base/strings/pattern.h"
#include "base/strings/string_util.h"
#include "base/trace_event/trace_event.h"
#include "base/trace_event/trace_id_helper.h"
#include "net/base/io_buffer.h"
#include "services/network/shared_dictionary/shared_dictionary_manager_on_disk.h"
#include "services/network/shared_dictionary/shared_dictionary_on_disk.h"
#include "services/network/shared_dictionary/shared_dictionary_writer_on_disk.h"
#include "url/scheme_host_port.h"

namespace network {

// This is a RefCounted subclass of SharedDictionaryOnDisk. This is used to
// share a SharedDictionaryOnDisk for multiple concurrent network requests.
class SharedDictionaryStorageOnDisk::RefCountedSharedDictionary
    : public SharedDictionaryOnDisk,
      public base::RefCounted<RefCountedSharedDictionary> {
 public:
  // `on_deleted_closure_runner` will be called when `this` is deleted.
  RefCountedSharedDictionary(
      size_t size,
      const net::SHA256HashValue& hash,
      const base::UnguessableToken& disk_cache_key_token,
      SharedDictionaryDiskCache& disk_cahe,
      base::OnceClosure disk_cache_error_callback,
      base::ScopedClosureRunner on_deleted_closure_runner)
      : SharedDictionaryOnDisk(size,
                               hash,
                               disk_cache_key_token,
                               &disk_cahe,
                               std::move(disk_cache_error_callback)),
        on_deleted_closure_runner_(std::move(on_deleted_closure_runner)) {}

 private:
  friend class RefCounted<RefCountedSharedDictionary>;
  ~RefCountedSharedDictionary() override = default;

  base::ScopedClosureRunner on_deleted_closure_runner_;
};

// This is a subclass of SharedDictionaryOnDisk. This holds a reference to a
// RefCountedSharedDictionary.
class SharedDictionaryStorageOnDisk::WrappedSharedDictionary
    : public SharedDictionary {
 public:
  explicit WrappedSharedDictionary(
      scoped_refptr<RefCountedSharedDictionary> ref_counted_shared_dictionary)
      : ref_counted_shared_dictionary_(
            std::move(ref_counted_shared_dictionary)) {}

  WrappedSharedDictionary(const WrappedSharedDictionary&) = delete;
  WrappedSharedDictionary& operator=(const WrappedSharedDictionary&) = delete;

  // SharedDictionary
  int ReadAll(base::OnceCallback<void(int)> callback) override {
    return ref_counted_shared_dictionary_->ReadAll(std::move(callback));
  }
  scoped_refptr<net::IOBuffer> data() const override {
    return ref_counted_shared_dictionary_->data();
  }
  size_t size() const override {
    return ref_counted_shared_dictionary_->size();
  }
  const net::SHA256HashValue& hash() const override {
    return ref_counted_shared_dictionary_->hash();
  }

 private:
  scoped_refptr<RefCountedSharedDictionary> ref_counted_shared_dictionary_;
};

SharedDictionaryStorageOnDisk::SharedDictionaryStorageOnDisk(
    base::WeakPtr<SharedDictionaryManagerOnDisk> manager,
    const net::SharedDictionaryIsolationKey& isolation_key,
    base::ScopedClosureRunner on_deleted_closure_runner)
    : manager_(manager),
      isolation_key_(isolation_key),
      on_deleted_closure_runner_(std::move(on_deleted_closure_runner)) {
  manager_->metadata_store().GetDictionaries(
      isolation_key_,
      base::BindOnce(&SharedDictionaryStorageOnDisk::OnDatabaseRead,
                     weak_factory_.GetWeakPtr()));
}

SharedDictionaryStorageOnDisk::~SharedDictionaryStorageOnDisk() = default;

std::unique_ptr<SharedDictionary> SharedDictionaryStorageOnDisk::GetDictionary(
    const GURL& url) {
  if (!manager_) {
    return nullptr;
  }
  net::SharedDictionaryInfo* info =
      GetMatchingDictionaryFromDictionaryInfoMap(dictionary_info_map_, url);
  if (!info) {
    return nullptr;
  }

  manager_->UpdateDictionaryLastUsedTime(*info);

  auto it = dictionaries_.find(info->disk_cache_key_token());
  if (it != dictionaries_.end()) {
    CHECK_EQ(info->size(), it->second->size());
    CHECK(info->hash() == it->second->hash());
    return std::make_unique<WrappedSharedDictionary>(it->second.get());
  }

  auto ref_counted_shared_dictionary = base::MakeRefCounted<
      RefCountedSharedDictionary>(
      info->size(), info->hash(), info->disk_cache_key_token(),
      manager_->disk_cache(),
      base::BindOnce(
          &SharedDictionaryManagerOnDisk::MaybePostMismatchingEntryDeletionTask,
          manager_),
      base::ScopedClosureRunner(base::BindOnce(
          &SharedDictionaryStorageOnDisk::OnRefCountedSharedDictionaryDeleted,
          weak_factory_.GetWeakPtr(), info->disk_cache_key_token())));
  dictionaries_.emplace(info->disk_cache_key_token(),
                        ref_counted_shared_dictionary.get());
  return std::make_unique<WrappedSharedDictionary>(
      std::move(ref_counted_shared_dictionary));
}

scoped_refptr<SharedDictionaryWriter>
SharedDictionaryStorageOnDisk::CreateWriter(const GURL& url,
                                            base::Time response_time,
                                            base::TimeDelta expiration,
                                            const std::string& match) {
  if (!manager_) {
    return nullptr;
  }
  return manager_->CreateWriter(
      isolation_key_, url, response_time, expiration, match,
      base::BindOnce(&SharedDictionaryStorageOnDisk::OnDictionaryWritten,
                     weak_factory_.GetWeakPtr()));
}

void SharedDictionaryStorageOnDisk::OnDatabaseRead(
    net::SQLitePersistentSharedDictionaryStore::DictionaryListOrError result) {
  CHECK(dictionary_info_map_.empty());
  if (!result.has_value()) {
    return;
  }
  std::set<base::UnguessableToken> deleted_cache_tokens;
  for (auto& info : result.value()) {
    const url::SchemeHostPort scheme_host_port =
        url::SchemeHostPort(info.url());
    const std::string match = info.match();
    (dictionary_info_map_[scheme_host_port])
        .insert(std::make_pair(match, std::move(info)));
  }
}

void SharedDictionaryStorageOnDisk::OnDictionaryWritten(
    net::SharedDictionaryInfo info) {
  const url::SchemeHostPort scheme_host_port = url::SchemeHostPort(info.url());
  const std::string match = info.match();
  (dictionary_info_map_[scheme_host_port])
      .insert_or_assign(match, std::move(info));
}

void SharedDictionaryStorageOnDisk::OnRefCountedSharedDictionaryDeleted(
    const base::UnguessableToken& disk_cache_key_token) {
  dictionaries_.erase(disk_cache_key_token);
}

void SharedDictionaryStorageOnDisk::OnDictionaryDeleted(
    const std::set<base::UnguessableToken>& disk_cache_key_tokens) {
  std::erase_if(dictionaries_, [&disk_cache_key_tokens](const auto& it) {
    return disk_cache_key_tokens.find(it.first) != disk_cache_key_tokens.end();
  });

  for (auto& it1 : dictionary_info_map_) {
    std::erase_if(it1.second, [&disk_cache_key_tokens](const auto& it2) {
      return disk_cache_key_tokens.find(it2.second.disk_cache_key_token()) !=
             disk_cache_key_tokens.end();
    });
  }
  std::erase_if(dictionary_info_map_,
                [](const auto& it) { return it.second.empty(); });
}

}  // namespace network
