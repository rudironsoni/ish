#ifndef MEM_OBJECT_H
#define MEM_OBJECT_H

#include <IXLandLinuxRuntime/util/list.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stddef.h>
#include <unistd.h>

/* Forward declaration to break circular dependency with fd.h */
struct fd;

/*
 * mem_object replaces the old struct data.
 *
 * Key differences from the old model:
 * - Backing objects are NEVER freed immediately on unmap.
 * - Instead they are retired with a generation stamp and placed on a
 *   deferred-reclamation list. Actual munmap+free happens only at a
 *   proven-safe drain point (epoch boundary).
 * - This guarantees that any lockless translation (TLB miss path, TCTI
 *   block execution) that grabbed a reference to a mem_object will never
 *   observe freed memory.
 *
 * Object kinds distinguish fast-path RAM from special/helper mappings.
 * The TLB MUST NOT cache a host delta for a kind that requires helper
 * semantics.
 */

enum mem_object_kind {
    MEM_OBJ_RAM,     /* anonymous mmap - fast path eligible */
    MEM_OBJ_FILE,    /* file-backed mmap - fast path eligible */
    MEM_OBJ_VDSO,    /* built-in VDSO page - fast path eligible */
    MEM_OBJ_SPECIAL, /* helper-only, never TLB-cached as RAM */
};

struct mem_object {
    void *host_base;           /* host mapping base (mmap'd or static) */
    size_t host_size;          /* host VM span length (page-rounded) */
    enum mem_object_kind kind; /* fast-path vs helper-only */
    atomic_uint refcount;

    /* file-backed metadata */
    struct fd *fd;
    size_t file_offset;
    const char *name; /* for /proc/pid/maps display */

    /* deferred reclamation */
    uint64_t retire_generation; /* generation at retire time */
    struct list retire_link;    /* link on retire list */
};

/* Allocate a new mem_object. Takes ownership of host_base. */
struct mem_object *mem_object_new(void *host_base, size_t host_size, enum mem_object_kind kind);

/* Increment / decrement refcount. Free + munmap happens at refcount==0
   ONLY if the object is not on the retire list (i.e., still mapped).
   When unmapped, the object goes to the retire list and is freed later. */
void mem_object_retain(struct mem_object *obj);
void mem_object_release(struct mem_object *obj);

/* Mark an object as retired (no longer mapped in any page table).
   The object stays alive until the retire list is drained. */
void mem_object_retire(struct mem_object *obj);

/* Drain the retire list: free all objects whose retire_generation is
   strictly less than the current active generation. This is the safe
   point where actual munmap+free occurs. */
void mem_object_drain_retired(uint64_t current_generation);

#endif
