#include "numa_queue.h"

extern inline void numa_enqueue(Globals* context, long thread_id, long task) {
#ifdef MSQUEUE
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Tail);
#else
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Tail->tail);
#endif
    enqueue(&context->queue, thread_id, task);
#ifdef MSQUEUE
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail);
#else
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail->tail);
#endif
    spin_work();
}

extern inline void numa_dequeue(Globals* context, long task){
#ifdef MSQUEUE
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Head);
#else
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Head->head);
#endif
    // FIXME
    dequeue(&context->queue, task);
#ifdef MSQUEUE
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail);
#else
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail->tail);
#endif
    spin_work();
}

extern inline Globals* create_global_context() {
    Globals* context = (Globals*)malloc(sizeof(Globals));
    SHARED_OBJECT_INIT(&context->queue);
#ifdef MSQUEUE
    cluster_scheduler_init(&context->atomic_scheduler, &context->queue.Head, &context->queue.Tail);
#else
    cluster_scheduler_init(&context->atomic_scheduler, &context->queue.Head->head, &context->queue.Tail->tail);
#endif
    return context;
}
