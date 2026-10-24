/*
 * queue.c -- implements the generic queue interface in queue.h.
 */
#include "queue.h"

#include <stddef.h>
#include <stdlib.h>

/* A queue node associates one caller-owned element with the next node. */
typedef struct queue_node {
    void *element;
    struct queue_node *next;
} queue_node_t;

/* Front and rear pointers make the core queue operations constant time. */
typedef struct queue_representation {
    queue_node_t *front;
    queue_node_t *rear;
} queue_representation_t;

/*
 * qopen -- allocate and return a new empty queue.
 *
 * Returns NULL if allocation fails.
 */
queue_t *qopen(void)
{
    queue_representation_t *queue;

    /* calloc initializes both queue links to NULL. */
    queue = calloc(1, sizeof(queue_representation_t));
    return (queue_t *)queue;
}

/*
 * qclose -- free a queue and its nodes without freeing caller-owned elements.
 *
 * A NULL queue requires no work.
 */
void qclose(queue_t *qp)
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *current;
    queue_node_t *next;

    /* Release each internal node, then release the queue representation. */
    if (queue != NULL) {
        current = queue->front;
        while (current != NULL) {
            next = current->next;
            free(current);
            current = next;
        }
        free(queue);
    }
}

/*
 * qput -- add an element to the rear of a queue in constant time.
 *
 * Returns 0 for success; non-zero for invalid arguments or allocation failure.
 */
int32_t qput(queue_t *qp, void *elementp)
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *node;
    int32_t status = 1;

    /* Allocate a node only when both the queue and element are valid. */
    if (queue != NULL && elementp != NULL) {
        node = malloc(sizeof(queue_node_t));
        if (node != NULL) {
            node->element = elementp;
            node->next = NULL;
            /* The first node becomes both the front and rear. */
            if (queue->rear == NULL) {
                queue->front = node;
            } else {
                queue->rear->next = node;
            }
            queue->rear = node;
            status = 0;
        }
    }

    return status;
}

/*
 * qget -- remove and return the element at the front in constant time.
 *
 * Returns NULL when the queue is invalid or empty.
 */
void *qget(queue_t *qp)
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *node;
    void *element = NULL;

    /* Remove the front node while preserving ownership of its element. */
    if (queue != NULL && queue->front != NULL) {
        node = queue->front;
        queue->front = node->next;
        /* Removing the final node also empties the rear link. */
        if (queue->front == NULL) {
            queue->rear = NULL;
        }
        element = node->element;
        free(node);
    }

    return element;
}

/*
 * qapply -- apply a supplied function to every queued element.
 *
 * A NULL queue or function requires no work.
 */
void qapply(queue_t *qp, void (*fn)(void *elementp))
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *current;

    /* Visit elements in queue order without modifying the queue. */
    if (queue != NULL && fn != NULL) {
        for (current = queue->front; current != NULL; current = current->next) {
            fn(current->element);
        }
    }
}

/*
 * qsearch -- find the first element accepted by a supplied search function.
 *
 * Returns the matching element, or NULL when no match can be found.
 */
void *qsearch(queue_t *qp,
              bool (*searchfn)(void *elementp, const void *keyp),
              const void *skeyp)
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *current;
    void *result = NULL;

    /* Stop at the first match so queue order determines duplicate handling. */
    if (queue != NULL && searchfn != NULL) {
        current = queue->front;
        while (current != NULL && result == NULL) {
            if (searchfn(current->element, skeyp)) {
                result = current->element;
            } else {
                current = current->next;
            }
        }
    }

    return result;
}

/*
 * qremove -- find, unlink, and return the first matching element.
 *
 * Returns the matching element, or NULL when no match can be found.
 */
void *qremove(queue_t *qp,
              bool (*searchfn)(void *elementp, const void *keyp),
              const void *skeyp)
{
    queue_representation_t *queue = (queue_representation_t *)qp;
    queue_node_t *current;
    queue_node_t *previous = NULL;
    void *result = NULL;

    /* Track the previous node so any matching node can be unlinked. */
    if (queue != NULL && searchfn != NULL) {
        current = queue->front;
        while (current != NULL && result == NULL) {
            if (searchfn(current->element, skeyp)) {
                result = current->element;
                /* Repair the front link or the preceding node's link. */
                if (previous == NULL) {
                    queue->front = current->next;
                } else {
                    previous->next = current->next;
                }
                /* Removing the rear node requires a new rear pointer. */
                if (queue->rear == current) {
                    queue->rear = previous;
                }
                /* Free only the node; the caller receives the element. */
                free(current);
                current = NULL;
            } else {
                previous = current;
                current = current->next;
            }
        }
    }

    return result;
}

/*
 * qconcat -- append q2 to q1 in constant time and deallocate q2.
 *
 * Invalid, missing, or identical queues require no work.
 */
void qconcat(queue_t *q1p, queue_t *q2p)
{
    queue_representation_t *q1 = (queue_representation_t *)q1p;
    queue_representation_t *q2 = (queue_representation_t *)q2p;

    /* Move q2's existing links without traversing either queue. */
    if (q1 != NULL && q2 != NULL && q1 != q2) {
        if (q2->front != NULL) {
            /* An empty q1 directly adopts q2's front. */
            if (q1->rear == NULL) {
                q1->front = q2->front;
            } else {
                q1->rear->next = q2->front;
            }
            q1->rear = q2->rear;
        }
        /* Only q2's empty representation remains after the links move. */
        free(q2);
    }
}
