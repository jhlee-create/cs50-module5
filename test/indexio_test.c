/* Save and reload an example index to confirm that its counts do not change. */
#include "indexio.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

/* Use the index functions to create known values and read them back. */
index_t *index_new(void);
int32_t index_add(index_t *index, const char *word, int document_id);
int index_get_count(index_t *index, const char *word, int document_id);
int index_word_count(index_t *index);
uint64_t sumwords(index_t *index);

int main(int argc, char *argv[])
{
    index_t *original = NULL;
    index_t *reloaded = NULL;
    int status = EXIT_FAILURE;

    /* Require a filename so the example index can be saved and reloaded. */
    if (argc != 2) {
        fprintf(stderr, "usage: indexio_test <index-file>\n");
        return status;
    }

    /* Create known counts so the loaded result has exact expected values. */
    original = index_new();
    if (original != NULL &&
        index_add(original, "apple", 1) == 0 &&
        index_add(original, "apple", 1) == 0 &&
        index_add(original, "apple", 3) == 0 &&
        index_add(original, "banana", 2) == 0 &&
        index_add(original, "banana", 2) == 0 &&
        index_add(original, "banana", 2) == 0 &&
        index_add(original, "banana", 2) == 0 &&
        indexsave(original, argv[1]) == 0) {
        reloaded = indexload(argv[1]);
    }

    /* Check every known value so missing or changed data causes failure. */
    if (reloaded != NULL && index_word_count(reloaded) == 2 &&
        index_get_count(reloaded, "apple", 1) == 2 &&
        index_get_count(reloaded, "apple", 3) == 1 &&
        index_get_count(reloaded, "banana", 2) == 4 &&
        sumwords(reloaded) == 7) {
        printf("PASS indexio: loaded 2 words and 7 total occurrences\n");
        status = EXIT_SUCCESS;
    } else {
        fprintf(stderr, "FAIL indexio: saved and loaded indexes differ\n");
    }

    /* Free both indexes after the comparison is complete. */
    indexdelete(original);
    indexdelete(reloaded);

    return status;
}
