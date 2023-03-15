//
// Created by antares on 3/13/23.
//

#include "ThreadPool.h"

#if defined(_MSC_FULL_VER)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#endif
#include <Windows.h>
#undef WIN32_LEAN_AND_MEAN
#include <processthreadsapi.h>
#include <string>

inline void *platform_thread_self()
{
    return GetCurrentThread();
}

inline void platform_set_thread_name(void *platform_thread_self, const char *name)
{
    size_t len = strlen(name);
    std::wstring wst(len + 1, '#');
    size_t num_convert;
    mbstowcs_s(&num_convert, &wst[0], len + 1, name, len + 1);
    SetThreadDescription(platform_thread_self, &wst[0]);
}

inline void platform_get_thread_name(void *platform_thread_self, char *buf, size_t bufsize)
{
    wchar_t *wbuf;
    GetThreadDescription(platform_thread_self, &wbuf);
    size_t num_convert;
    wcstombs_s(&num_convert, buf, bufsize, wbuf, bufsize);
    LocalFree(wbuf);
}

#else // HEDLEY_MSVC_VERSION

#if defined(__GNUC__)

#include <pthread.h>

inline auto platform_thread_self() {
    return pthread_self();
}

inline void platform_set_thread_name(decltype(platform_thread_self()) id, const char *name) {
    pthread_setname_np(id, name);
}

inline void platform_get_thread_name(decltype(platform_thread_self()) id, char *buf, size_t bufsize) {
    pthread_getname_np(id, buf, bufsize);
}

#endif
#endif

namespace Antares {

    void ThreadPoolBase::worker() {
        platform_set_thread_name(platform_thread_self(), "Worker");
        std::mutex t_mtx;
        while (running) {
            std::function<void()> task;
            std::unique_lock<std::mutex> tasks_lock(t_mtx);
            task_available_cv.wait(tasks_lock, [this] { return !tasks.empty() || !running; });
            tasks_lock.unlock();
            if (!paused) {
                auto popresult = tasks.pop(task);
                if (!popresult) continue;
                task();
                --tasks_total;
                if (waiting)
                    task_done_cv.notify_one();
            }
        }
    }

    ThreadPoolBase::ThreadPoolBase() {}

    concurrency_t ThreadPoolBase::determine_thread_count(const concurrency_t thread_count_) {
        if (thread_count_ > 0)
            return thread_count_;
        else {
            if (std::thread::hardware_concurrency() > 0)
                return std::thread::hardware_concurrency();
            else
                return 1;
        }
    }

    void ThreadPoolBase::wait_for_tasks() {
        waiting = true;
        static std::mutex unused_mtx;
        std::unique_lock<std::mutex> tasks_lock(unused_mtx);
        task_done_cv.wait(tasks_lock, [this] { return (tasks_total == (paused ? tasks.size() : 0)); });
        waiting = false;
    }

    void ThreadPoolBase::pause() {
        paused = true;
    }

    size_t ThreadPoolBase::get_tasks_total() const {
        return tasks_total;
    }

    bool ThreadPoolBase::is_paused() const {
        return paused;
    }

    void ThreadPoolBase::unpause() {
        paused = false;
    }
}