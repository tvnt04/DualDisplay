#pragma once

#include <cstddef>
#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

inline void* alignedAllocate(std::size_t size, std::size_t alignment) noexcept
{
    if (size == 0)
        return nullptr;

    std::size_t pageAlign = size % 4096;
    if (pageAlign)
    {
        size += (4096 - pageAlign);
    }

#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* memory = nullptr;
    if (posix_memalign(&memory, alignment, size) != 0)
        return nullptr;
    return memory;
#endif
}

inline void alignedFree(void* memory) noexcept
{
#ifdef _WIN32
    _aligned_free(memory);
#else
    std::free(memory);
#endif
}