#ifndef CPPREFERENCE_THREAD_POOL_HPP
#define CPPREFERENCE_THREAD_POOL_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace cppreference::thread_pool {

class ThreadPool {
public:
    explicit ThreadPool(std::size_t num_threads,
                        std::chrono::seconds default_timeout = std::chrono::hours(1));
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args> // NOLINTNEXTLINE(readability-identifier-length)
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>>;

    void wait_all();
    void wait_all_with_timeout(std::chrono::seconds timeout);
    std::size_t active_tasks() const;

private:
    void worker_loop();

    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;
    std::mutex queue_mutex_;
    std::condition_variable cv_;
    std::condition_variable finished_cv_;
    std::atomic<bool> stop_{false};
    std::atomic<std::size_t> active_tasks_{0};
    mutable std::mutex finished_mutex_;
    std::chrono::seconds default_timeout_;
};

template <typename Func, typename... Args>
auto ThreadPool::submit(Func&& func, Args&&... args) -> std::future<std::invoke_result_t<Func, Args...>> {
    using return_type = std::invoke_result_t<Func, Args...>;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        [func = std::forward<Func>(func), ...args = std::forward<Args>(args)]() mutable {
            return func(std::forward<Args>(args)...);
        }
    );

    std::future<return_type> result = task->get_future();

    {
        std::lock_guard<std::mutex> lock{queue_mutex_};
        if (stop_) {
            throw std::runtime_error("Submit on stopped ThreadPool");
        }
        tasks_.emplace([task]() { (*task)(); });
    }
    cv_.notify_one();

    return result;
}

} // namespace cppreference::thread_pool

#endif // CPPREFERENCE_THREAD_POOL_HPP
