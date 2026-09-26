#ifndef PAGE_ALLOCATOR_H
#define PAGE_ALLOCATOR_H

#include <cstddef>
#include <cstdlib>
#include <new>
#include <unistd.h>

// Allocates whole, page-aligned pages. The histogram uses it so buddha-metal can hand the same
// memory to the GPU (Metal requires page-aligned, page-sized storage to wrap memory without a
// copy); for the CPU renderer it is an ordinary allocation.
template <class T> struct page_allocator {
    typedef T value_type;

    page_allocator() = default;
    template <class U> page_allocator(const page_allocator<U> &) {}

    static size_t page_size() { return size_t(sysconf(_SC_PAGESIZE)); }

    // Bytes actually allocated for n elements.
    static size_t allocation_size(size_t n) {
        const size_t page = page_size();
        return (n * sizeof(T) + page - 1) / page * page;
    }

    T *allocate(size_t n) {
        void *memory = nullptr;
        if (posix_memalign(&memory, page_size(), allocation_size(n)) != 0)
            throw std::bad_alloc();
        return static_cast<T *>(memory);
    }

    void deallocate(T *p, size_t) noexcept { free(p); }

    template <class U> bool operator==(const page_allocator<U> &) const { return true; }
};

#endif
