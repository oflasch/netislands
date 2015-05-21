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

typedef void (*QueueMapping)(void *element, void *args);

typedef int (*QueueEqualPredicate)(const void *a, const void *b);


int queue_init(Queue *queue); 

long queue_length(const Queue *queue);

int queue_enqueue(Queue *queue, const void *data); 
int queue_add_front(Queue *queue, const void *data);

int queue_dequeue(Queue *queue, void **data); 
int queue_remove_index(Queue *queue, const long index, void **data); 

int queue_get_index(const Queue *queue, const long index, void **data);

void queue_for_each(const Queue *queue, const QueueMapping f, void *f_args); 
long queue_first_index_of(const Queue *queue, const void *what, const QueueEqualPredicate equal_predicate); 


#endif

