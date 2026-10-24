#pragma once
/* 
 * queue.h -- public interface to the queue module
 */
#include <stdint.h>
#include <stdbool.h>

/* the queue representation is hidden from users of the module */
typedef void queue_t;		

/* create an empty queue */
queue_t* qopen(void);        

/* deallocate the queue and its nodes; queued elements remain caller-owned */
void qclose(queue_t *qp);   

/* put an element at the end of the queue
 * returns 0 if successful; nonzero otherwise
 */
int32_t qput(queue_t *qp, void *elementp); 

/* get and remove the first element from the queue */
void* qget(queue_t *qp);

/* apply a function to every element of the queue */
void qapply(queue_t *qp, void (*fn)(void* elementp));

/* search a queue using a supplied boolean function
 * skeyp -- a key to search for
 * searchfn -- a function applied to every element of the queue
 *          -- elementp - a pointer to an element
 *          -- keyp - the key being searched for (i.e. will be 
 *             set to skey at each step of the search
 *          -- returns TRUE or FALSE as defined in bool.h
 * returns a pointer to an element, or NULL if not found
 */
void* qsearch(queue_t *qp, 
							bool (*searchfn)(void* elementp,const void* keyp),
							const void* skeyp);

/* search a queue using a supplied boolean function (as in qsearch),
 * removes the element from the queue and returns a pointer to it or
 * NULL if not found
 */
void* qremove(queue_t *qp,
							bool (*searchfn)(void* elementp,const void* keyp),
							const void* skeyp);

/* concatenate the elements of q2 onto q1
 * q2 is deallocated and unusable upon completion
 */
void qconcat(queue_t *q1p, queue_t *q2p);

