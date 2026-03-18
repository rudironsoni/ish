/*
 * Block cache for compiled aarch64 TCTI blocks
 *
 * Simple hash table implementation using iSH's list primitives.
 */

#include "emu/aarch64/block-cache.h"
#include <stdlib.h>
#include <string.h>

void a64_cache_init(struct a64_block_cache *cache) {
    memset(cache, 0, sizeof(*cache));

    // Initialize hash buckets
    for (int i = 0; i < BLOCK_CACHE_HASH_SIZE; i++) {
        list_init(&cache->hash[i]);
    }

    list_init(&cache->jetsam);
    // Simple implementation without locks for now
    // lock_init(&cache->lock);
    cache->num_blocks = 0;
}

struct a64_block *a64_cache_lookup(struct a64_block_cache *cache, uint64_t pc) {
    // Simple implementation without locks for now
    // lock_read(&cache->lock);

    size_t idx = a64_cache_hash(pc);
    struct list *bucket = &cache->hash[idx];

    struct a64_block *block;
    list_for_each_entry(bucket, block, chain) {
        if (block->start_pc == pc && !block->is_jetsam) {
            // lock_unlock(&cache->lock);
            return block;
        }
    }

    // lock_unlock(&cache->lock);
    return NULL;
}

void a64_cache_insert(struct a64_block_cache *cache, struct a64_block *block) {
    // lock_write(&cache->lock);

    size_t idx = a64_cache_hash(block->start_pc);
    list_add(&cache->hash[idx], &block->chain);

    cache->num_blocks++;

    // lock_unlock(&cache->lock);
}

void a64_cache_invalidate_all(struct a64_block_cache *cache) {
    // lock_write(&cache->lock);

    // Mark all blocks as jetsam
    for (int i = 0; i < BLOCK_CACHE_HASH_SIZE; i++) {
        struct a64_block *block;
        list_for_each_entry(&cache->hash[i], block, chain) {
            if (!block->is_jetsam) {
                block->is_jetsam = true;
                list_add(&cache->jetsam, &block->jetsam);
            }
        }
    }

    // lock_unlock(&cache->lock);
}

void a64_block_free(struct a64_block *block) {
    if (!block) return;

    list_remove(&block->chain);
    list_remove(&block->jetsam);

    if (block->gadgets) {
        free(block->gadgets);
    }

    free(block);
}
