#ifndef MEMORYBUFFER_H
#define MEMORYBUFFER_H

#include <Arduino.h>

/**
 * @class MemoryBuffer
 * @brief A class that handles memory allocation and deallocation for a buffer.
 *
 * This class provides an RAII approach to manage a dynamically allocated buffer. It ensures that memory is
 * allocated during object creation and automatically freed when the object goes out of scope.
 *
 * @note It is recommended to use the `MemoryBuffer` class when dealing with dynamic memory allocation,
 *       to avoid memory leaks and ensure proper memory management.
 *
 * Example use:
 * ```cpp
 * {
 *     MemoryBuffer buffer(512);
 *     if (buffer.isAllocated()) { // Check if allocated!
 *         // Access buffer here...
 *     } else {
 *         // Handle error (e.g., log error, retry later)
 *     }
 * } // buffer automatically freed
 *
 * ```
 */
class MemoryBuffer
{
public:
    /**
     * @brief Constructs a `MemoryBuffer` object and allocates memory of the specified size.
     *
     * The constructor allocates memory of the specified size for the buffer. If allocation fails,
     * the buffer will not be valid.
     *
     * @param size The size of the buffer in bytes.
     *
     * @example
     * // Example usage of the constructor
     * MemoryBuffer buffer(512);  // Allocates a buffer of 512 bytes
     */
    explicit MemoryBuffer(size_t size);

    /**
     * @brief Destructor that frees the allocated memory.
     *
     * The destructor automatically frees the memory allocated for the buffer when the object is destroyed.
     */
    ~MemoryBuffer();

    /**
     * @brief Returns a pointer to the allocated memory buffer.
     *
     * @return A pointer to the allocated memory, or `nullptr` if memory allocation failed.
     */
    uint8_t *get();

    /**
     * @brief Returns the size of the allocated buffer.
     *
     * @return The size of the allocated buffer in bytes.
     */
    size_t size() const;

    /**
     * @brief Checks whether memory allocation was successful.
     *
     * @return `true` if memory was successfully allocated, `false` if the buffer is `nullptr`.
     */
    bool isAllocated();

private:
    size_t size_;     // Size of the allocated buffer
    uint8_t *buffer_; // Pointer to the allocated buffer
};

#endif // MEMORYBUFFER_H
