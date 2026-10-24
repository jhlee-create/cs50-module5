/* Build an index showing how often each accepted word appears on each page. */
#include "hash.h"
#include "indexio.h"
#include "pageio.h"
#include "queue.h"
#include "webpage.h"

#include <ctype.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Keep one running total so every stored page count can be added together. */
static uint64_t running_total = 0;

/* Keep one running count so every different word can be counted. */
static int running_word_count = 0;

/* Copy the word because the extracted word is freed after it is counted. */
static char *copy_word(const char *word)
{
    char *copy = NULL;
    size_t length = 0;

    /* Include '\0' so the copy is a complete C string. */
    if (word != NULL) {
        length = strlen(word) + 1;
        copy = malloc(length);
        if (copy != NULL) {
            memcpy(copy, word, length);
        }
    }

    return copy;
}

/* Compare word text so the requested word can be found in the index. */
static bool word_matches(void *elementp, const void *searchkeyp)
{
    index_word_t *entry = elementp;
    const char *requested_word = searchkeyp;
    bool matches = false;

    /* Check both words before comparing them to avoid invalid memory access. */
    if (entry != NULL && entry->word != NULL && requested_word != NULL &&
        strcmp(entry->word, requested_word) == 0) {
        matches = true;
    }

    return matches;
}

/* Compare page IDs so the requested page count can be found. */
static bool document_matches(void *elementp, const void *searchkeyp)
{
    index_document_t *entry = elementp;
    const int *requested_id = searchkeyp;
    bool matches = false;

    /* Check that both values exist before comparing their page IDs. */
    if (entry != NULL && requested_id != NULL &&
        entry->document_id == *requested_id) {
        matches = true;
    }

    return matches;
}

/* Free a new word when an error prevents the index from keeping it. */
static void delete_unstored_word(index_word_t *entry)
{
    index_document_t *document;

    /* Free the contents before their containers so no memory is left behind. */
    if (entry != NULL) {
        document = qget(entry->documents);
        while (document != NULL) {
            free(document);
            document = qget(entry->documents);
        }
        qclose(entry->documents);
        free(entry->word);
        free(entry);
    }
}

/* Keep only useful words and make their spelling consistent for counting. */
bool NormalizeWord(char *word)
{
    size_t position;
    size_t length = 0;
    bool valid = false;

    /* Reject missing words and words shorter than three letters. */
    if (word != NULL) {
        length = strlen(word);
        if (length >= 3) {
            valid = true;
        }
    }

    /* Reject nonletters so the index contains alphabetic words only. */
    position = 0;
    while (valid && position < length) {
        if (!isalpha((unsigned char)word[position])) {
            valid = false;
        }
        position++;
    }

    /* Use lowercase so words such as "Page" and "page" share one count. */
    if (valid) {
        for (position = 0; position < length; position++) {
            word[position] = (char)tolower((unsigned char)word[position]);
        }
    }

    return valid;
}

/* Create an empty index before any page words are added. */
index_t *index_new(void)
{
    return hopen(INDEX_BUCKETS);
}

/* Update the word's page count each time that word is found. */
int32_t index_add(index_t *index, const char *word, int document_id)
{
    index_word_t *word_entry;
    index_document_t *document_entry;
    int32_t status = 1;
    bool new_word = false;

    /* Reject input that does not identify a valid word and page. */
    if (index == NULL || word == NULL || *word == '\0' || document_id <= 0) {
        return status;
    }

    /* Find the word first so its page counts can be updated. */
    word_entry = hsearch(index, word_matches, word, (int32_t)strlen(word));

    /* Create a word and page list when this is its first occurrence. */
    if (word_entry == NULL) {
        word_entry = calloc(1, sizeof(index_word_t));
        if (word_entry != NULL) {
            word_entry->word = copy_word(word);
            word_entry->documents = qopen();
        }
        if (word_entry == NULL || word_entry->word == NULL ||
            word_entry->documents == NULL) {
            delete_unstored_word(word_entry);
            return status;
        }
        new_word = true;
    }

    /* Reuse the page count when present; otherwise start it at one. */
    document_entry = qsearch(word_entry->documents, document_matches,
                             &document_id);
    if (document_entry != NULL) {
        if (document_entry->count < INT_MAX) {
            document_entry->count++;
            status = 0;
        }
    } else {
        document_entry = malloc(sizeof(index_document_t));
        if (document_entry != NULL) {
            document_entry->document_id = document_id;
            document_entry->count = 1;
            status = qput(word_entry->documents, document_entry);
            if (status != 0) {
                free(document_entry);
            }
        }
    }

    /* Store a new word only after its first page count was created. */
    if (new_word && status == 0) {
        status = hput(index, word_entry, word_entry->word,
                      (int)strlen(word_entry->word));
    }

    /* Free the new data after a failed addition so it is not leaked. */
    if (new_word && status != 0) {
        delete_unstored_word(word_entry);
    }

    return status;
}

/* Find and return the count for one word on one page. */
int index_get_count(index_t *index, const char *word, int document_id)
{
    index_word_t *word_entry;
    index_document_t *document_entry;
    int count = 0;

    /* Search only that word's pages after the word itself is found. */
    word_entry = NULL;
    if (index != NULL && word != NULL && *word != '\0' && document_id > 0) {
        word_entry = hsearch(index, word_matches, word,
                             (int32_t)strlen(word));
    }
    if (word_entry != NULL) {
        document_entry = qsearch(word_entry->documents, document_matches,
                                 &document_id);
        if (document_entry != NULL) {
            count = document_entry->count;
        }
    }

    return count;
}

/* Increase the total once for each stored word that is visited. */
static void count_word(void *elementp)
{
    /* Ignore a missing word because there is nothing to count. */
    if (elementp != NULL) {
        running_word_count++;
    }
}

/* Visit every stored word to count how many different words exist. */
int index_word_count(index_t *index)
{
    /* Reset the count so earlier calls do not affect this result. */
    running_word_count = 0;
    happly(index, count_word);
    return running_word_count;
}

/* Add each page count to the total number of word occurrences. */
static void sum_document(void *elementp)
{
    index_document_t *document = elementp;

    /* Ignore a missing page entry because it has no count to add. */
    if (document != NULL) {
        running_total += (uint64_t)document->count;
    }
}

/* Visit every page for one word so all of its counts are included. */
static void sum_word(void *elementp)
{
    index_word_t *word = elementp;

    /* Skip a missing word because it has no page counts. */
    if (word != NULL) {
        qapply(word->documents, sum_document);
    }
}

/* Add every page count so the stored total can be checked. */
uint64_t sumwords(index_t *index)
{
    /* Reset the total so earlier calls do not affect this result. */
    running_total = 0;
    happly(index, sum_word);
    return running_total;
}

/* Scan one page so each accepted word is added to that page's counts. */
int32_t index_page(index_t *index, webpage_t *page, int document_id,
                   uint64_t *word_countp)
{
    char *word = NULL;
    int position = 0;
    int next_position;
    int32_t status = 0;

    /* Remember where this word ends so the next search continues after it. */
    next_position = webpage_getNextWord(page, position, &word);
    while (next_position > 0 && status == 0) {
        position = next_position;

        /* Normalize the word so only accepted lowercase words are counted. */
        if (NormalizeWord(word)) {
            status = index_add(index, word, document_id);
            if (status == 0 && word_countp != NULL) {
                (*word_countp)++;
            }
        }

        /* Free the temporary word because the index keeps its own copy. */
        free(word);
        word = NULL;

        /* Stop after an error so no later words are added to a failed index. */
        if (status == 0) {
            next_position = webpage_getNextWord(page, position, &word);
        }
    }

    /* Free a remaining temporary word when processing stops early. */
    free(word);

    return status;
}

/* Leave out main when another program uses only the index functions. */
#ifndef INDEXER_TEST_BUILD

/* Print the expected arguments so an invalid command can be corrected. */
static void usage(void)
{
    fprintf(stderr, "usage: indexer <pagedir> <indexnm>\n");
}

/* Check the page directory before trying to load numbered files from it. */
static bool pagedir_is_readable(const char *pagedir)
{
    struct stat directory_info;
    bool valid = false;

    /* Require both a directory and permission to read and enter it. */
    if (pagedir != NULL && stat(pagedir, &directory_info) == 0 &&
        S_ISDIR(directory_info.st_mode) &&
        access(pagedir, R_OK | X_OK) == 0) {
        valid = true;
    }

    return valid;
}

/* Load consecutive numbered pages so all of them contribute to one index. */
static int32_t build_index(const char *pagedir, index_t *index,
                           int *page_countp, uint64_t *word_countp)
{
    webpage_t *page;
    int document_id = 1;
    int32_t status = 0;

    /* Stop at the first missing page because page IDs must be consecutive. */
    page = pageload(document_id, (char *)pagedir);
    while (page != NULL && status == 0) {
        status = index_page(index, page, document_id, word_countp);
        webpage_delete(page);
        page = NULL;

        /* Count only a page whose accepted words were added successfully. */
        if (status == 0) {
            (*page_countp)++;
            document_id++;
            page = pageload(document_id, (char *)pagedir);
        }
    }

    /* Treat a missing page 1 as an error because indexing must begin there. */
    if (status == 0 && *page_countp == 0) {
        fprintf(stderr, "indexer: pagedir contains no page 1\n");
        status = 1;
    }

    /* Free a loaded page that could not be completely indexed. */
    webpage_delete(page);

    return status;
}

int main(int argc, char *argv[])
{
    index_t *index = NULL;
    int page_count = 0;
    uint64_t word_count = 0;
    int32_t status = 1;

    /* Reject the command unless it provides both required paths. */
    if (argc != 3) {
        usage();
    } else if (!pagedir_is_readable(argv[1])) {
        /* Stop before loading when the page directory cannot be used. */
        fprintf(stderr, "indexer: pagedir must be a readable directory\n");
        usage();
    } else {
        /* Build the index in memory before writing the output file. */
        index = index_new();
        if (index != NULL &&
            build_index(argv[1], index, &page_count, &word_count) == 0) {
            /* Save only when the accepted-word total matches the stored total. */
            if (sumwords(index) == word_count) {
                /* Write the index so it remains after the program ends. */
                status = indexsave(index, argv[2]);
            }
        }
        if (status != 0) {
            fprintf(stderr, "indexer: could not create the index\n");
        }
    }

    /* Free the in-memory index after it has been saved or an error occurs. */
    indexdelete(index);

    /* Print totals only after the index file was saved successfully. */
    if (status == 0) {
        printf("indexed %d pages and %llu normalized words\n",
               page_count, (unsigned long long)word_count);
    }

    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
#endif
