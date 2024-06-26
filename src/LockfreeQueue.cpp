//
// Created by antares on 3/13/23.
//

#include "LockfreeQueue.h"
#include "thirdparty/concurrentqueue.h"


namespace Antares {
/// modify this to change the default behavior
    struct LockfreeQueueDefaultTraits : moodycamel::ConcurrentQueueDefaultTraits {
    };

    using QueueType = moodycamel::ConcurrentQueue<std::function<void()>, LockfreeQueueDefaultTraits>;

    QueueType *translate(void *p) {
        return (QueueType *) p;
    }

    QueueType &toref(void *p) {
        return *translate(p);
    }

    LockfreeQueue::LockfreeQueue() : queueWrappingPointer(new QueueType) {
    }

    LockfreeQueue::~LockfreeQueue() {
        auto t = translate(queueWrappingPointer);
        queueWrappingPointer = nullptr;
        delete t;
    }

    void LockfreeQueue::push(std::function<void()> &&func) {
        toref(queueWrappingPointer).enqueue(std::move(func));
        _counter++;
    }

    bool LockfreeQueue::pop(std::function<void()> &store) {
        auto result = toref(queueWrappingPointer).try_dequeue(store);
        if (result) _counter--;
        return result;
    }
}
