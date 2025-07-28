/**
 * @file aesd-circular-buffer.c
 * @brief Functions and data related to a circular buffer imlementation
 *
 * @author Dan Walkes
 * @date 2020-03-01
 * @copyright Copyright (c) 2020
 *
 */

#ifdef __KERNEL__
#include <linux/string.h>
#else
#include <string.h>
#endif

#include "aesd-circular-buffer.h"

/**
 * @param buffer the buffer to search for corresponding offset.  Any necessary locking must be performed by caller.
 * @param char_offset the position to search for in the buffer list, describing the zero referenced
 *      character index if all buffer strings were concatenated end to end
 * @param entry_offset_byte_rtn is a pointer specifying a location to store the byte of the returned aesd_buffer_entry
 *      buffptr member corresponding to char_offset.  This value is only set when a matching char_offset is found
 *      in aesd_buffer.
 * @return the struct aesd_buffer_entry structure representing the position described by char_offset, or
 * NULL if this position is not available in the buffer (not enough data is written).
 */
struct aesd_buffer_entry *aesd_circular_buffer_find_entry_offset_for_fpos(
    struct aesd_circular_buffer *buffer,
    size_t char_offset, 
    size_t *entry_offset_byte_rtn )
{
    /**
    * DONE: implement per description
    */
    size_t acc_length = 0;
    size_t offset = buffer->out_offs;
    size_t current_pos;

    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i) {
        // Compute current pos in buffer with output offset
        current_pos = (offset + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;

        // If offset falls in current pos, return.
        if (acc_length + buffer->entry[current_pos].size > char_offset) {
            *entry_offset_byte_rtn = char_offset - acc_length; 
            return &buffer->entry[current_pos];
        }
           
        acc_length += buffer->entry[current_pos].size;
    }

    return NULL;
}

/**
* Adds entry @param add_entry to @param buffer in the location specified in buffer->in_offs.
* If the buffer was already full, overwrites the oldest entry and advances buffer->out_offs to the
* new start location.
* Any necessary locking must be handled by the caller
* Any memory referenced in @param add_entry must be allocated by and/or must have a lifetime managed by the caller.
*/
const char *aesd_circular_buffer_add_entry(
    struct aesd_circular_buffer *buffer, 
    const struct aesd_buffer_entry *add_entry )
{
    /**
    * DONE: implement per description
    */
    //Store old entry text pointer if present.
    const char *removed_buffptr = NULL;
    if (buffer->entry[buffer->in_offs].buffptr) {
        removed_buffptr = buffer->entry[buffer->in_offs].buffptr;
    }

    // Write new entry.
    buffer->entry[buffer->in_offs] = *add_entry;
    buffer->in_offs += 1;

    // Loop index if required.
    if (buffer->in_offs == AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED) {
        buffer->in_offs = 0;
        buffer->full = true; // After first loop this is noop.
    }

    // When looping, move output offset to new start location.
    if (buffer->full) {
        buffer->out_offs = buffer->in_offs;
    }

    return removed_buffptr;
}

/**
* Initializes the circular buffer described by @param buffer to an empty struct
*/
void aesd_circular_buffer_init(struct aesd_circular_buffer* buffer)
{
    memset(buffer,0,sizeof(struct aesd_circular_buffer));
}

/**
* Computer the total size of the circular buffer 
*/
size_t aesd_circular_buffer_size(struct aesd_circular_buffer *buffer) {
    struct aesd_buffer_entry *entry;
    uint8_t offset = buffer->out_offs;
    uint8_t current_pos;

    size_t buffer_size = 0;

    for (uint8_t i = 0; i < AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED; ++i) {
        current_pos = (offset + i) % AESDCHAR_MAX_WRITE_OPERATIONS_SUPPORTED;
        entry = &buffer->entry[current_pos];
        buffer_size += entry->size;
    }

    return buffer_size;
}

