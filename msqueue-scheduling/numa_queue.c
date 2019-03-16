#include "numa_queue.h"

extern void numa_enqueue(Globals* context, Object arg, int pid) {
#ifdef MSQUEUE
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Tail);
#else
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Tail->tail);
#endif
    enqueue(&context->queue, arg, pid);
#ifdef MSQUEUE
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail);
#else
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Tail->tail);
#endif

    spin_work();
}

extern Object numa_dequeue(Globals* context, int pid){
    Object obj;

#ifdef MSQUEUE
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Head);
#else
    cluster_scheduler_start_op(&context->atomic_scheduler, &context->queue.Head->head);
#endif
    obj = (Object)(dequeue(&context->queue, pid));
#ifdef MSQUEUE
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Head);
#else
    cluster_scheduler_end_op(&context->atomic_scheduler, &context->queue.Head->head);
#endif

    spin_work();

    return obj;
}

extern Globals* create_global_context() {
    Globals* context = (Globals*)malloc(sizeof(Globals));
    SHARED_OBJECT_INIT(&context->queue);
#ifdef MSQUEUE
    cluster_scheduler_init(&context->atomic_scheduler, &context->queue.Head, &context->queue.Tail);
#else
    cluster_scheduler_init(&context->atomic_scheduler, &context->queue.Head->head, &context->queue.Tail->tail);
#endif
    return context;
}
