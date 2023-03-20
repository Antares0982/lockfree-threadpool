//
// Created by antares on 3/15/23.
//

#ifndef LOCKFREE_THREADPOOL_ALLOCATOR_H
#define LOCKFREE_THREADPOOL_ALLOCATOR_H

#include <memory>

namespace Antares::details {
    template<typename T, typename MemoryTraits>
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
            auto result = (T *) MemoryTraits::malloc(n * sizeof(T));;
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
            MemoryTraits::free(p);
        }
    };
}
#endif //LOCKFREE_THREADPOOL_ALLOCATOR_H
