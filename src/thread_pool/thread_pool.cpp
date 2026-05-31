#include "thread_pool.hpp"
#include <iostream>

namespace cppreference::thread_pool {

namespace {

constexpr std::size_t default_num_threads = 8;

} // namespace

ThreadPool::ThreadPool(std::size_t num_threads, std::chrono::seconds default_timeout)
    : default_timeout_{default_timeout}
{
    if (num_threads == 0) {
        num_threads = default_num_threads;
    }
    workers_.reserve(num_threads);
    for (std::size_t idx = 0; idx < num_threads; ++idx) {
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock{queue_mutex_};
        stop_ = true;
    }
    cv_.notify_all();

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock{queue_mutex_};
            cv_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

            if (stop_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        ++active_tasks_;

        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "ThreadPool: task threw std::exception: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "ThreadPool: task threw unknown exception\n";
        }

        {
            std::lock_guard<std::mutex> lock{finished_mutex_};
            --active_tasks_;
        }
        finished_cv_.notify_all();
    }
}

void ThreadPool::wait_all() {
    wait_all_with_timeout(default_timeout_);
}

void ThreadPool::wait_all_with_timeout(std::chrono::seconds timeout) {
    std::unique_lock<std::mutex> lock{finished_mutex_};
    bool completed = false;
    completed = finished_cv_.wait_for(lock, timeout, [this] {
        std::lock_guard<std::mutex> q_lock{queue_mutex_};
        return active_tasks_ == 0 && tasks_.empty();
    });

    if (!completed) {
        std::lock_guard<std::mutex> q_lock{queue_mutex_};
        std::cerr << "ThreadPool::wait_all timed out after " << timeout.count()
                  << " seconds — " << active_tasks_.load() << " tasks still active, "
                  << tasks_.size() << " queued\n";
    }
}

std::size_t ThreadPool::active_tasks() const {
    return active_tasks_;
}

} // namespace cppreference::thread_pool
