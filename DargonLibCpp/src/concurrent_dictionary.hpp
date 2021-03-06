#pragma once

#include <array>
#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <numeric>
#include <unordered_map>

// determined by a dice roll between 13 17 19 23 29 and 31
#define CONCURRENT_DICTIONARY_BUCKET_COUNT ((int)25)

namespace dargon { 

   template <typename TKey,
             typename TValue,
             class KeyHash = std::hash<TKey>,
             class KeyEqualityComparer = std::equal_to<TKey>,
             class PairAllocator = std::allocator<std::pair<const TKey, TValue>>>
   class concurrent_dictionary_bucket
   {
   public:
      typedef concurrent_dictionary_bucket<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> my_t;
      typedef std::mutex MutexType;
      typedef std::lock_guard<MutexType> LockType;
      typedef std::pair<const TKey, TValue> PairType;

   private:
      const my_t* next;
      std::atomic<std::size_t> count;
      mutable std::mutex mutex;
      std::unordered_map<const TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> dict;

   public:
      concurrent_dictionary_bucket(const my_t* next) : count(0), next(next) { }
      ~concurrent_dictionary_bucket() { }

      TValue get_value_or_default(const TKey key) {
         LockType lock(mutex);
         auto it = dict.find(key);
         if (it == dict.end()) {
            TValue value = {};
            return value;
         } else {
            return it->second;
         }
      }

      void add_or_update(const TKey& key, std::function<TValue(const TKey&)> add, std::function<TValue(const TKey&, TValue)> update) {
         LockType lock(mutex);
         auto it = dict.find(key);
         if (it == dict.end()) {
            dict.insert(PairType(key, add(key)));
            count++;
         } else {
            it->second = update(key, it->second);
         }
      }

      bool conditional_remove(const TKey& key, std::function<bool(const TKey&, const TValue&)> remove_if) {
         LockType lock(mutex);
         auto it = dict.find(key);
         if (it == dict.end()) {
            return false;
         } else {
            bool remove = remove_if(it->first, it->second);
            if (remove) {
               dict.erase(it);
               count--;
            }
            return remove;
         }
      }

      bool insert(const TKey key, TValue value) {
         LockType lock(mutex);
         if (dict.insert(PairType(key, value)).second) {
            count++;
            return true;
         }
         return false;
      }

      bool remove(const TKey key) {
         LockType lock(mutex);
         if (dict.erase(key)) {
            count--;
            return true;
         }
         return false;
      }

      bool contains(const TKey key) const {
         LockType lock(mutex);
         return dict.find(key) != dict.end();
      }

      std::size_t size() const { return count; }

      const my_t* next_bucket() const { return next; }

      std::vector<PairType> copy_pairs() const {
         LockType lock(mutex);
         std::vector<PairType> results(dict.begin(), dict.end());
         //for (auto kvp : dict) {
         //   results.emplace_back(kvp);
         //}
         return results;
      }
   };

   template <typename TKey, 
             typename TValue, 
             class KeyHash = std::hash<TKey>, 
             class KeyEqualityComparer = std::equal_to<TKey>,
             class PairAllocator = std::allocator<std::pair<const TKey, TValue>>>
   class concurrent_dictionary_iterator : public std::iterator<std::forward_iterator_tag, std::pair<const TKey, TValue>>
   {
      typedef concurrent_dictionary_iterator<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> my_t;
      typedef concurrent_dictionary_bucket<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> bucket_t;
      typedef std::pair<const TKey, TValue> value_t;
      typedef std::vector<value_t, PairAllocator> pairs_t;

   private:
      const bucket_t* bucket;
      std::shared_ptr<pairs_t> currentPairs;
      int currentProgress;

   public:
      /*
      
         while (this->bucket != nullptr && this->bucket->size() == 0) { this->bucket = this->bucket->next_bucket(); }
         if (this->bucket) {
            currentPairs = this->bucket->copy_pairs();
         }
         */
      concurrent_dictionary_iterator() : concurrent_dictionary_iterator(nullptr, 0) { }
      explicit concurrent_dictionary_iterator(bucket_t* bucket, int currentProgress) : bucket(bucket), currentProgress(currentProgress) { 
         while (this->bucket != nullptr) {
            if (this->bucket->size() > 0) {
               currentPairs = std::make_shared<pairs_t>(this->bucket->copy_pairs());
               if (currentPairs->size() > 0) {
                  break;
               }
            } else {
               this->bucket = this->bucket->next_bucket();
            }
         }
      }
      concurrent_dictionary_iterator(const my_t& iterator) : bucket(iterator.bucket), currentProgress(iterator.currentProgress), currentPairs(iterator.currentPairs) { }

      my_t operator++() { increment(); return *this; }
      my_t operator++(int) { my_t copy(*this); increment(); return copy; }

      value_t& operator* () { return currentPairs->at(currentProgress); }
      value_t* operator-> () { return currentPairs ? &currentPairs->at(currentProgress) : nullptr; }

      bool operator==(const my_t& other) { return bucket == other.bucket && currentProgress == other.currentProgress; }
      bool operator!=(const my_t& other) { return bucket != other.bucket || currentProgress != other.currentProgress; }

      void increment() { 
         if (currentProgress + 1 == currentPairs->size()) {
            currentProgress = 0;
            currentPairs.reset();
            do {
               bucket = bucket->next_bucket();
               if (bucket && bucket->size() > 0) {
                  currentPairs = std::make_shared<pairs_t>(bucket->copy_pairs());
               }
            } while (bucket && !currentPairs);
         } else {
            ++currentProgress;
         }
      }
   };

   template <typename TKey,
             typename TValue,
             class KeyHash = std::hash<TKey>,
             class KeyEqualityComparer = std::equal_to<TKey>,
             class PairAllocator = std::allocator<std::pair<const TKey, TValue>>>
   class concurrent_dictionary
   {
      typedef concurrent_dictionary<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> my_t;
      typedef concurrent_dictionary_bucket<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> bucket;
      typedef concurrent_dictionary_iterator<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator> iterator;

      mutable std::array<std::unique_ptr<bucket>, CONCURRENT_DICTIONARY_BUCKET_COUNT> buckets;
      mutable KeyHash key_hash;

      bucket* GetBucket(const TKey& key) const { return buckets[(unsigned int)(key_hash(key)) % CONCURRENT_DICTIONARY_BUCKET_COUNT].get(); }

   public:
      concurrent_dictionary() : key_hash() { 
         for (auto i = CONCURRENT_DICTIONARY_BUCKET_COUNT - 1; i >= 0; --i) {
            auto next = i + 1 == CONCURRENT_DICTIONARY_BUCKET_COUNT ? nullptr : buckets[i + 1].get();
            buckets[i] = std::move(std::unique_ptr<bucket>(new bucket(next)));
         }
      }

      TValue get_value_or_default(const TKey& key) {
         return GetBucket(key)->get_value_or_default(key);
      }

      void add_or_update(const TKey& key, std::function<TValue(const TKey&)> add, std::function<TValue(const TKey&, TValue)> update) {
         return GetBucket(key)->add_or_update(key, add, update);
      }

      bool conditional_remove(const TKey& key, std::function<bool(const TKey&, const TValue&)> remove_if) {
         return GetBucket(key)->conditional_remove(key, remove_if);
      }

      bool insert(const TKey key, TValue value) {
         return GetBucket(key)->insert(key, value);
      }

      bool remove(const TKey& key) {
         return GetBucket(key)->remove(key);
      }

      inline bool erase(const TKey& key) { return remove(key); }

      bool contains(const TKey& key) const {
         return GetBucket(key)->contains(key);
      }

      size_t size() const { return std::accumulate(buckets.begin(), buckets.end(), 0, [](size_t totalCount, std::unique_ptr<bucket>& bucket) { return totalCount += bucket->size(); }); }

      iterator begin() const { return my_t::iterator(buckets[0].get(), 0); }
      iterator end() const { return my_t::iterator(nullptr, 0); }
   };

   template <typename TKey,
             typename TValue,
             class KeyHash = std::hash<TKey>,
             class KeyEqualityComparer = std::equal_to<TKey>,
             class PairAllocator = std::allocator<std::pair<const TKey, TValue>>>
   using concurrent_map = concurrent_dictionary<TKey, TValue, KeyHash, KeyEqualityComparer, PairAllocator>;
} 