#include "stdafx.h"
#include "CppUnitTest.h"
#include <random>
#include <string>
#include <thread>
#include <unordered_set>
#include <concurrent_set.hpp>
#include <countdown_event.hpp>

using namespace Microsoft::VisualStudio::CppUnitTestFramework;

namespace dargon {
   TEST_CLASS(ConcurrentSetTests) {
      typedef unsigned int TKey;
      typedef concurrent_set<unsigned int> TDictionary;
      typedef std::numeric_limits<TKey> KeyLimits;

   public:
      TDictionary dict;

      TEST_METHOD_INITIALIZE(Setup) {}

      TEST_METHOD(SingleThreadedTest) {
         const unsigned int entryCount = 10000;
         for (auto i = 0U; i < entryCount; i++) {
            dict.insert(i);
         }
         for (auto i = 0U; i < entryCount; i++) {
            Assert::IsTrue(dict.remove(i));
         }
      }

      TEST_METHOD(MultiThreadedTest) {
         const unsigned int threadCount = 16;
         const unsigned int keyLowerBound = 0;
         const unsigned int keyUpperBound = (KeyLimits::max() >> 1) + 1;
         const unsigned long long prime = 387420489;
         const unsigned long long maximum = KeyLimits::max();
         const unsigned int skip = ((keyUpperBound - keyLowerBound) / threadCount);
         const unsigned int keysPerThread = skip / (pow(2U, 16));

         Assert::AreEqual(2048U, (unsigned int)keysPerThread);
         Assert::AreEqual(0, std::distance(dict.begin(), dict.end()));

         std::vector<std::thread> threads;
         std::vector<std::vector<TKey>> pairsByThreadId;
         countdown_event beginAddSignal(1);
         countdown_event endAddSignal(threadCount);
         countdown_event beginRemoveSignal(1);
         countdown_event endRemoveSignal(threadCount);
         countdown_event beginAddAndRemoveSignal(1);
         countdown_event endAddAndRemoveSignal(1);
         countdown_event cleanedUpSignal(threadCount);
         for (auto t = 0U; t < threadCount; t++) {
            auto lowerInclusive = t * skip;
            std::vector<TKey> pairs;
            for (auto i = 0; i < keysPerThread; i++) {
               auto encoded = (unsigned int)((((unsigned long long)(lowerInclusive + i)) * prime) % maximum);
               pairs.emplace_back(encoded);
            }
            pairsByThreadId.emplace_back(std::move(pairs));

            threads.emplace_back(std::thread(
               std::bind(&ConcurrentSetTests::AddRemoveThread, this, pairsByThreadId[t], &beginAddSignal, &endAddSignal, &beginRemoveSignal, &endRemoveSignal, &beginAddAndRemoveSignal, &endAddAndRemoveSignal, &cleanedUpSignal)
               ));
         }
         beginAddSignal.signal();
         endAddSignal.wait();
         Assert::AreEqual(keysPerThread * threadCount, dict.size());
         Assert::AreEqual((ptrdiff_t)(keysPerThread * threadCount), std::distance(dict.begin(), dict.end()));

         beginRemoveSignal.signal();
         endRemoveSignal.wait();
         Assert::AreEqual(0U, dict.size());
         Assert::AreEqual(0, std::distance(dict.begin(), dict.end()));

         std::unordered_set<unsigned int> items;
         for (auto i = 0; i < 10000; i++) {
            auto lowerInclusive = keyUpperBound + 10;
            auto encoded = (unsigned int)((((unsigned long long)(lowerInclusive + i)) * prime) % maximum);
            dict.insert(encoded);
            items.insert(encoded);
         }

         beginAddAndRemoveSignal.signal();
         auto now = std::chrono::system_clock::now();
         auto duration = std::chrono::seconds(5);
         auto end = now + duration;
         for (auto it = 0; it < 3 || std::chrono::system_clock::now() < end; it++) {
            for (auto value : items) {
               Assert::IsTrue(dict.contains(value));
            }
            size_t matchCount = 0;
            for (auto item : dict) {
               if (items.find(item) != items.end()) {
                  matchCount++;
               }
            }
            Assert::AreEqual(items.size(), matchCount);
            matchCount = 0;
            for (auto it = dict.begin(); it != dict.end(); it++) {
               if (items.find(*it) != items.end()) {
                  matchCount++;
               }
            }
            Assert::AreEqual(items.size(), matchCount);
         }
         endAddAndRemoveSignal.signal();

         for (auto item : items) {
            Assert::IsTrue(dict.remove(item));
         }
         cleanedUpSignal.wait();

         Assert::AreEqual(0U, dict.size());

         for (auto it = threads.begin(); it != threads.end(); it++) {
            it->join();
         }
      }

   private:
      void AddRemoveThread(std::vector<TKey>& values, const countdown_event* beginAddSignal, countdown_event* endAddSignal, const countdown_event* beginRemoveSignal, countdown_event* endRemoveSignal, const countdown_event* beginAddAndRemoveSignal, const countdown_event* endAddAndRemoveSignal, countdown_event* cleanedUpSignal) {
         beginAddSignal->wait();
         for (auto value : values) {
            dict.insert(value);
         }
         endAddSignal->signal();

         beginRemoveSignal->wait();
         for (auto value : values) {
            Assert::IsTrue(dict.remove(value));
         }
         endRemoveSignal->signal();

         beginAddAndRemoveSignal->wait();
         std::unordered_set<TKey> added;
         std::random_device rd;
         std::mt19937 mt(rd());
         std::uniform_int_distribution<TKey> dist(0, values.size() - 1);
         while (!endAddAndRemoveSignal->wait(0)) {
            for (auto i = 0; i < values.size(); i++) {
               auto value = values[dist(mt)];
               if (added.find(value) == added.end()) {
                  added.insert(value);
                  dict.insert(value);
               } else {
                  added.erase(value);
                  dict.remove(value);
               }
            }
         }
         for (auto key : added) {
            dict.remove(key);
         }
         cleanedUpSignal->signal();
      }
   };
}