//
// Created by antares on 3/13/23.
//

#ifndef TESTPROJECT_THREADPOOL_H
#define TESTPROJECT_THREADPOOL_H


#include <type_traits>
#include <future>
#include <functional>
#include "LockfreeQueue.h"

namespace Antares {

#define THREAD_POOL_VERSION "v1.0.0"

    using concurrency_t = decltype(std::thread::hardware_concurrency());

    struct ThreadPoolDefaultTraits {
        static inline void *malloc(size_t size) {
            return ::malloc(size);
        }

        static inline void free(void *ptr) {
            ::free(ptr);
        }
    };

    class ThreadPoolBase {
    protected:
        template<typename T1, typename T2, typename T = std::common_type_t<T1, T2>>
        class [[nodiscard]] blocks {
        public:
            blocks(const T1 first_index_, const T2 index_after_last_, const size_t num_blocks_) : first_index(
                    static_cast<T>(first_index_)), index_after_last(static_cast<T>(index_after_last_)), num_blocks(
                    num_blocks_) {
                if (index_after_last < first_index)
                    std::swap(index_after_last, first_index);
                total_size = static_cast<size_t>(index_after_last - first_index);
                block_size = static_cast<size_t>(total_size / num_blocks);
                if (block_size == 0) {
                    block_size = 1;
                    num_blocks = (total_size > 1) ? total_size : 1;
                }
            }

            [[nodiscard]] T start(const size_t i) const {
                return static_cast<T>(i * block_size) + first_index;
            }

            [[nodiscard]] T end(const size_t i) const {
                return (i == num_blocks - 1) ? index_after_last : (static_cast<T>((i + 1) * block_size) + first_index);
            }

            [[nodiscard]] size_t get_num_blocks() const {
                return num_blocks;
            }

            [[nodiscard]] size_t get_total_size() const {
                return total_size;
            }

        private:
            size_t block_size = 0;
            T first_index = 0;
            T index_after_last = 0;
            size_t num_blocks = 0;
            size_t total_size = 0;
        };

    protected:
        std::atomic<size_t> tasks_total = 0;
        std::condition_variable task_available_cv = {};
        std::atomic<bool> running = false;
        std::atomic<bool> waiting = false;
        std::condition_variable task_done_cv = {};
        std::atomic<bool> paused = false;
        LockfreeQueue tasks; // this class implements its own traits

    public:
        ThreadPoolBase();

        ~ThreadPoolBase() = default;

        void wait_for_tasks();

        void pause();

        [[nodiscard]] size_t get_tasks_total() const;

        /// may not be accurate!
        [[nodiscard]] size_t get_tasks_queued() const {
            return tasks.size();
        }

        /// may not be accurate!
        [[nodiscard]] size_t get_tasks_running() const {
            // const std::scoped_lock tasks_lock(tasks_mutex);
            return get_tasks_total() - tasks.size();
        }

        [[nodiscard]] bool is_paused() const;

        void unpause();

    protected:
        [[nodiscard]] concurrency_t determine_thread_count(concurrency_t thread_count_);

        void worker();


    };

    template<typename Traits = ThreadPoolDefaultTraits>
    class ThreadPool : public ThreadPoolBase {
        template<typename T>
        struct Allocator : public std::allocator<T> {
            Allocator(const Allocator &) = default;

            Allocator() = default;

            Allocator(Allocator &&) = default;

            ~Allocator() = default;

            [[nodiscard]] T *allocate(size_t n
#if __cplusplus <= 201703L
                    const void* hint = nullptr
#endif
            ) {
                auto result = (T *) Traits::malloc(n * sizeof(T));;
                if (!result)std::__throw_bad_array_new_length();
                return result;
            }

#if __cplusplus >= 202106L
            std::allocation_result<T*, std::size_t> allocate_at_least( std::size_t n ){
            auto result = allocate(n);
            return {result, n};
        }
#endif

            void deallocate(T *p, std::size_t n) {
                Traits::free(p);
            }
        };

        std::vector<std::thread, Allocator<std::thread>> threads;

    public:
        ThreadPool(concurrency_t thread_count_ = 0)
                : ThreadPoolBase(),
                  threads(determine_thread_count(thread_count_)) {
            create_threads();
        }

        ~ThreadPool() {
            wait_for_tasks();
            destroy_threads();
        }

        template<typename T>
        static T *Create() {
            auto ptr = (T *) Traits::malloc(sizeof(T));
            new(ptr)T;
            return ptr;
        }

        template<typename T>
        static void Destroy(T *promise) {
            promise->~T();
            Traits::free(promise);
        }

        template<typename F, typename... A>
        void push_task(F &&task, A &&...args) {
            std::function<void()> task_function = std::bind(std::forward<F>(task), std::forward<A>(args)...);

            tasks.push(std::move(task_function));

            ++tasks_total;
            task_available_cv.notify_one();
        }

        template<typename F, typename... A, typename R = std::invoke_result_t<std::decay_t<F>, std::decay_t<A>...>>
        [[nodiscard]] std::future<R> submit(F &&task, A &&...args) {
            auto task_promise = Create<std::promise<R>>();
            auto future = task_promise->get_future();
            push_task(
                    [task_promise](auto &&taskInner, auto &&...argss) {
                        try {
                            if constexpr (std::is_void_v<R>) {
                                std::invoke(std::forward<decltype(taskInner)>(taskInner),
                                            std::forward<decltype(argss)>(argss)...);
                                task_promise->set_value();
                            } else {
                                task_promise->set_value(std::invoke(std::forward<decltype(taskInner)>(taskInner),
                                                                    std::forward<decltype(argss)>(argss)...));
                            }
                        } catch (...) {
                            try {
                                task_promise->set_exception(std::current_exception());
                            } catch (...) {
                            }
                        }
                        Destroy(task_promise);
                    },
                    std::forward<F>(task), std::forward<A>(args)...);
            return future;
        }

        template<typename F, typename T1, typename T2, typename T = std::common_type_t<T1, T2>>
        void push_loop(const T1 first_index, const T2 index_after_last, F &&loop, const size_t num_blocks = 0) {
            blocks blks(first_index, index_after_last, num_blocks ? num_blocks : threads.size());
            if (blks.get_total_size() > 0) {
                for (size_t i = 0; i < blks.get_num_blocks(); ++i)
                    push_task(std::forward<F>(loop), blks.start(i), blks.end(i));
            }
        }

        template<typename F, typename T>
        void push_loop(const T index_after_last, F &&loop, const size_t num_blocks = 0) {
            push_loop(0, index_after_last, std::forward<F>(loop), num_blocks);
        }

        template<typename R>
        using MultiFuture = std::vector<std::future<R>, Allocator<std::future<R>>>;

        template<typename F, typename T1, typename T2, typename T = std::common_type_t<T1, T2>, typename R = std::invoke_result_t<std::decay_t<F>, T, T>>
        [[nodiscard]] MultiFuture<R>
        parallelize_loop(const T1 first_index, const T2 index_after_last, F &&loop, const size_t num_blocks = 0) {
            blocks blks(first_index, index_after_last, num_blocks ? num_blocks : threads.size());
            if (blks.get_total_size() > 0) {
                MultiFuture <R> mf(blks.get_num_blocks());
                // const std::scoped_lock tasks_lock(tasks_mutex);
                for (size_t i = 0; i < blks.get_num_blocks(); ++i)
                    mf[i] = submit(std::forward<F>(loop), blks.start(i), blks.end(i));
                return mf;
            } else {
                return {};
            }
        }

        template<typename F, typename T, typename R = std::invoke_result_t<std::decay_t<F>, T, T>>
        [[nodiscard]] MultiFuture<R>
        parallelize_loop(const T index_after_last, F &&loop, const size_t num_blocks = 0) {
            return parallelize_loop(0, index_after_last, std::forward<F>(loop), num_blocks);
        }

        size_t get_thread_count() {
            return threads.size();
        }

        void reset(concurrency_t thread_count_ = 0) {
            const bool was_paused = paused;
            paused = true;
            wait_for_tasks();
            destroy_threads();
            auto thread_count = determine_thread_count(thread_count_);
            threads.resize(thread_count);
            paused = was_paused;
            create_threads();
        }

    private:
        void create_threads() {
            running = true;
            for (concurrency_t i = 0; i < threads.size(); ++i) {
                threads[i] = std::thread(&ThreadPool::worker, this);
            }
        }

        void destroy_threads() {
            running = false;
            task_available_cv.notify_all();
            for (concurrency_t i = 0; i < threads.size(); ++i) {
                threads[i].join();
            }
        }
    };
}
#endif //TESTPROJECT_THREADPOOL_H
