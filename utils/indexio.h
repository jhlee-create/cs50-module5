#pragma once
/* Share the word-index format with files that save, load, or use it. */
#include <stdint.h>

#include "hash.h"
#include "queue.h"

/* Give the complete collection of words the name index_t. */
typedef hashtable_t index_t;

/* Pair each page ID with the word's count on that page. */
typedef struct index_document {
    int document_id;
    int count;
} index_document_t;

/* Keep each word together with all of its page IDs and counts. */
typedef struct index_word {
    char *word;
    queue_t *documents;
} index_word_t;

/* Use 64 starting locations so words can be spread across the index. */
#define INDEX_BUCKETS 64

/* Save every word and page count so the index remains after the program. */
int32_t indexsave(index_t *index, const char *indexnm);

/* Load every word and page count so a saved index can be used again. */
index_t *indexload(const char *indexnm);

/* Free every part of the index when it is no longer needed. */
void indexdelete(index_t *index);
