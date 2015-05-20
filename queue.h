#ifndef QUEUE_H
#define QUEUE_H


typedef struct QueueNode {
  void *data;
  struct QueueNode *next;
} QueueNode;

typedef struct {
  QueueNode *front;
  QueueNode *rear;
  long length;
} Queue;

typedef void (*QueueIterator)(void *element, void *args);


int queue_init(Queue *queue); 

long queue_length(const Queue *queue);

int queue_enqueue(Queue *queue, const void *data); 

int queue_dequeue(Queue *queue, void **data); 

void queue_for_each(const Queue *queue, const QueueIterator f, void *f_args); 
 
#endif

