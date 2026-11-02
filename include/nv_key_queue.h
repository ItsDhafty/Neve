#ifndef NV_KEY_QUEUE_H_
#define NV_KEY_QUEUE_H_

#include <stdbool.h>
#include "nv_key_parser.h"

#define keyQueueSize 128

typedef struct KeyQueue {
    uint16_t front, back;
    Key keys[keyQueueSize];
} KeyQueue;

// Add a key to the queue
// To be called only by the input thread
bool keyQueueEnq(KeyQueue *queue, Key key);
// Remove a key from the queue
// To be calle only by the main thread
Key keyQueueDeq(KeyQueue *queue);

#endif // !NV_KEY_QUEUE_H_
