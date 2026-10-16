#pragma once

#include <cstddef>
#include <cstdlib>

#ifdef _WIN32
#include <malloc.h>
#endif

inline void* alignedAllocate(std::size_t size, std::size_t alignment) noexcept
{
    if (size == 0 || alignment < sizeof(void*) ||
        (alignment & (alignment - 1)) != 0 || alignment % sizeof(void*) != 0)
        return nullptr;

#ifdef _WIN32
    return _aligned_malloc(size, alignment);
#else
    void* memory = nullptr;
    return posix_memalign(&memory, alignment, size) == 0 ? memory : nullptr;
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