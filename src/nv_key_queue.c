#include "nv_key_queue.h"

#define Q_INC_IDX(idx) (((idx) + 1) % keyQueueSize)

bool keyQueueEnq(KeyQueue *queue, Key key) {
    if (Q_INC_IDX(queue->back) == queue->front) {
        return false;
    }
    queue->keys[queue->back] = key;
    queue->back = Q_INC_IDX(queue->back);
    return true;
}

Key keyQueueDeq(KeyQueue *queue) {
    if (queue->front == queue->back) {
        return 0;
    }
    Key key = queue->keys[queue->front];
    queue->front = Q_INC_IDX(queue->front);
    return key;
}
