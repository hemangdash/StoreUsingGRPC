#ifndef THREADPOOL_H
#define THREADPOOL_H

#pragma once

#include <atomic>
#include <list>
#include <vector>
#include <condition_variable>
#include <functional>

using std::cout;
using std::endl;
using std::move;
using std::thread;
using std::lock_guard;
using std::mutex;
using std::unique_lock;
using std::vector;
using std::list;
using std::atomic_int;
using std::atomic_bool;
using std::condition_variable;

class threadpool {
   public:
      threadpool(unsigned num_threads): thread_pool(num_threads), remaining(0), hold_flag(false), finish_flag(false) {
         for (unsigned int i = 0; i < num_threads; i++) {
            thread_pool[i] = move(thread([this, i]{
                  this->task();
               }));
         }
      }

      ~threadpool() {
         joinAll();
      }

      void addJob(std::function<void()> job) {
         lock_guard<mutex> lock(queue_mutex);
         job_queue.emplace_back(job);
         remaining++;
         is_available.notify_one();
      }
      
      void allWait() {
         if (remaining) {
            unique_lock<mutex> lock(wait_mutex);
            is_await.wait(lock, [this]{
                  return (this->remaining == 0);
               });
            lock.unlock();
         }
      }

      void joinAll() {
         if (!finish_flag) {
            allWait();
            hold_flag = true;
            is_available.notify_all();
            for (auto &thread : thread_pool) {
               if (thread.joinable()) {
                  thread.join();
               }
            }
            finish_flag = true;
         }
      }

   private:
      void task() {
         while (!hold_flag) {
            next_task()();
            remaining--;
            is_await.notify_one();
         }
      }

      std::function<void()> next_task() {
         std::function<void()> task;
         unique_lock<mutex> lock(queue_mutex);
         is_available.wait(lock, [this]()->bool{
               return (hold_flag || job_queue.size());
            });
         if (hold_flag) {
            task = []{};
            remaining++;
         } else {
            task = job_queue.front();
            job_queue.pop_front();
         }
         return task;
      }

      vector<thread> thread_pool;
      mutex queue_mutex;
      mutex wait_mutex;
      list<std::function<void()>> job_queue;
      atomic_int remaining;
      atomic_bool hold_flag;
      atomic_bool finish_flag;
      condition_variable is_available;
      condition_variable is_await;
};

#endif
