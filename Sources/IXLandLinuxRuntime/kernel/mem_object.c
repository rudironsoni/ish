#include <IXLandInstrumentationTracing/trace.h>
#include <IXLandLinuxRuntime/kernel/mem_object.h>
#include <stdlib.h>
#include <sys/mman.h>

struct mem_object *mem_object_new(void *host_base, size_t host_size, enum mem_object_kind kind)
{
    struct mem_object *obj = malloc(sizeof(struct mem_object));
    if (!obj)
        return NULL;
    *obj = (struct mem_object){
        .host_base = host_base,
        .host_size = host_size,
        .kind = kind,
        .refcount = ATOMIC_VAR_INIT(1),
        .fd = NULL,
        .file_offset = 0,
        .name = NULL,
        .retire_generation = 0,
        .retire_link = { NULL, NULL },
    };
    return obj;
}

void mem_object_retain(struct mem_object *obj)
{
    if (!obj)
        return;
    atomic_fetch_add(&obj->refcount, 1);
}

void mem_object_release(struct mem_object *obj)
{
    if (!obj)
        return;
    if (atomic_fetch_sub(&obj->refcount, 1) == 1) {
        /* Last reference. If not retired, munmap and free now.
           If retired, the drain path already handled it. */
        if (obj->retire_generation == 0) {
            if (obj->host_base != MAP_FAILED && obj->host_base != NULL) {
                /* VDSO and static mappings are not mmap'd */
                munmap(obj->host_base, obj->host_size);
            }
            if (obj->fd) {
                /* fd_close would go here but avoid circular deps;
                   caller is responsible for fd lifecycle */
            }
            free((void *)obj->name);
            free(obj);
        }
    }
}

void mem_object_retire(struct mem_object *obj)
{
    if (!obj)
        return;
    /* Object stays alive; it will be freed by drain_retired.
       We set retire_generation to a non-zero value to mark it as
       retired (not eligible for immediate free in release). */
    obj->retire_generation = 1; /* placeholder, set properly by caller */
    list_add(&obj->retire_link, /* retire list head */ NULL);
}

void mem_object_drain_retired(uint64_t current_generation)
{
    /* Placeholder: walk retire list, free objects whose
       retire_generation < current_generation.
       Full implementation requires a list head passed in. */
    (void)current_generation;
}
