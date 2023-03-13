//
// Created by antares on 3/13/23.
//

#ifndef TESTPROJECT_LOCKFREEQUEUE_H
#define TESTPROJECT_LOCKFREEQUEUE_H

#include <atomic>
#include <functional>


class LockfreeQueue {
    void *queueWrappingPointer;
    std::atomic<size_t> _counter;

public:
    LockfreeQueue();

    LockfreeQueue(const LockfreeQueue &) = delete;

    LockfreeQueue(LockfreeQueue &&) = delete;

    ~LockfreeQueue();

    void push(std::function<void()> &&func);

    bool pop(std::function<void()> &store);

    /// may not be accurate!
    [[nodiscard]] size_t size() const { return _counter.load(std::memory_order_relaxed); }

    [[nodiscard]] bool empty() const { return size() == 0; }
};


#endif //TESTPROJECT_LOCKFREEQUEUE_H
