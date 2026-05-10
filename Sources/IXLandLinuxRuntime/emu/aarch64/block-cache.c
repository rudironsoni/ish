/*
 * Block cache for compiled aarch64 TCTI blocks
 *
 * Simple hash table implementation using iSH's list primitives.
 */

#import <IXLandLinuxRuntime/emu/aarch64/block-cache.h>
#include <stdlib.h>
#include <string.h>

void a64_cache_init(struct a64_block_cache *cache)
{
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

struct a64_block *a64_cache_lookup_ex(struct a64_block_cache *cache, uint64_t pc,
                                      mem_generation_t current_generation,
                                      enum a64_cache_lookup_miss_reason *miss_reason_out)
{
    bool saw_jetsam = false;
    bool saw_generation_mismatch = false;
    size_t idx = a64_cache_hash(pc);
    struct list *bucket = &cache->hash[idx];

    struct a64_block *block;
    list_for_each_entry (bucket, block, chain) {
        if (block->start_pc != pc)
            continue;
        if (block->is_jetsam) {
            saw_jetsam = true;
            continue;
        }
        if (block->compile_generation != current_generation) {
            saw_generation_mismatch = true;
            continue;
        }
        if (miss_reason_out != NULL)
            *miss_reason_out = A64_CACHE_LOOKUP_HIT;
        return block;
    }

    if (miss_reason_out != NULL) {
        if (saw_jetsam)
            *miss_reason_out = A64_CACHE_LOOKUP_MISS_JETSAM;
        else if (saw_generation_mismatch)
            *miss_reason_out = A64_CACHE_LOOKUP_MISS_GENERATION;
        else
            *miss_reason_out = A64_CACHE_LOOKUP_MISS_NOT_FOUND;
    }
    return NULL;
}

struct a64_block *a64_cache_lookup(struct a64_block_cache *cache, uint64_t pc,
                                   mem_generation_t current_generation)
{
    return a64_cache_lookup_ex(cache, pc, current_generation, NULL);
}

void a64_cache_insert(struct a64_block_cache *cache, struct a64_block *block)
{
    // lock_write(&cache->lock);

    size_t idx = a64_cache_hash(block->start_pc);
    list_add(&cache->hash[idx], &block->chain);

    cache->num_blocks++;

    // lock_unlock(&cache->lock);
}

static void a64_block_mark_jetsam(struct a64_block_cache *cache, struct a64_block *block)
{
    if (!block || block->is_jetsam)
        return;

    block->is_jetsam = true;
    list_add(&cache->jetsam, &block->jetsam);
}

void a64_cache_invalidate_all(struct a64_block_cache *cache)
{
    // lock_write(&cache->lock);

    // Mark all blocks as jetsam
    for (int i = 0; i < BLOCK_CACHE_HASH_SIZE; i++) {
        struct a64_block *block;
        list_for_each_entry (&cache->hash[i], block, chain) {
            a64_block_mark_jetsam(cache, block);
        }
    }

    // lock_unlock(&cache->lock);
}

void a64_cache_invalidate_range(struct a64_block_cache *cache, uint64_t start_pc, uint64_t end_pc)
{
    if (!cache || start_pc >= end_pc)
        return;

    for (int i = 0; i < BLOCK_CACHE_HASH_SIZE; i++) {
        struct a64_block *block;
        list_for_each_entry (&cache->hash[i], block, chain) {
            if (block->start_pc < end_pc && block->end_pc > start_pc)
                a64_block_mark_jetsam(cache, block);
        }
    }
}

void a64_block_free(struct a64_block *block)
{
    if (!block)
        return;

    list_remove(&block->chain);
    list_remove(&block->jetsam);

    if (block->gadgets) {
        free(block->gadgets);
    }

    free(block);
}
