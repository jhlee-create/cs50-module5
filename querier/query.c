/* A program that searches the saved index file and prints matching crawler pages by rank. */
#include "hash.h"
#include "indexio.h"
#include "pageio.h"
#include "queue.h"
#include "webpage.h"

#include <ctype.h>
#include <dirent.h>
#include <inttypes.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/* Record whether a query entered by the user is empty, valid, invalid, or could not be processed. */
typedef enum query_status {
    QUERY_EMPTY,
    QUERY_VALID,
    QUERY_INVALID,
    QUERY_ERROR
} query_status_t;

/* Store a matching page's document ID and the calculated query rank together. */
typedef struct query_result {
    int document_id;
    uint64_t rank;
} query_result_t;

/* Store sorted results in an array and queue them for ordered output. */
typedef struct ranked_documents {
    query_result_t *storage;
    queue_t *documents;
} ranked_documents_t;

/* Record whether a page directory is valid, inaccessible, or uncrawled. */
typedef enum page_directory_status {
    PAGE_DIRECTORY_VALID,
    PAGE_DIRECTORY_INACCESSIBLE,
    PAGE_DIRECTORY_NOT_CRAWLED
} page_directory_status_t;

/* Print the command format after an invalid argument or option. */
static void usage(void)
{
    fprintf(stderr, "usage: query <pageDirectory> <indexFile> [-q]\n");
}

/* Read one full query line entered by the user; return NULL at EOF or after a read error. */
static char *read_query_line(FILE *input, bool *errorp)
{
    char *line = NULL;
    char *larger_line;
    size_t capacity = 128;
    size_t length = 0;
    int character = EOF;

    /* Allocate memory to store the query line and report if allocation fails. */
    if (errorp != NULL) {
        *errorp = false;
    }
    if (input != NULL && errorp != NULL) {
        line = malloc(capacity);
        if (line == NULL) {
            *errorp = true;
        }
    }

    /* Double the available space whenever the query outgrows it. */
    while (line != NULL &&
           (character = fgetc(input)) != '\n' && character != EOF) {
        if (length + 1 >= capacity) {
            if (capacity > SIZE_MAX / 2) {
                free(line);
                line = NULL;
                *errorp = true;
            } else {
                capacity *= 2;
                larger_line = realloc(line, capacity);
                if (larger_line == NULL) {
                    free(line);
                    line = NULL;
                    *errorp = true;
                } else {
                    line = larger_line;
                }
            }
        }

        /* If memory allocation succeeded, append the character to the query. */
        if (line != NULL) {
            line[length] = (char)character;
            length++;
        }
    }

    /* Distinguish a normal EOF from an actual input error. */
    if (input != NULL && ferror(input)) {
        free(line);
        line = NULL;
        *errorp = true;
    } else if (line != NULL && character == EOF && length == 0) {
        free(line);
        line = NULL;
    }

    /* Mark the end of the query text so C string functions can read it safely. */
    if (line != NULL) {
        line[length] = '\0';
    }

    return line;
}

/* Check whether a query word is the operator "and" or "or". */
static bool is_operator(const char *token)
{
    return token != NULL &&
           (strcmp(token, "and") == 0 || strcmp(token, "or") == 0);
}

/* Convert a query to lowercase words and reject invalid characters or operators. */
static query_status_t parse_query(char *line, char ***tokensp,
                                  size_t *token_countp)
{
    char **tokens = NULL;
    char **larger_tokens;
    char *token;
    size_t token_capacity = 0;
    size_t token_count = 0;
    size_t position;
    query_status_t status = QUERY_VALID;

    /* Return no words unless the complete query is valid. */
    if (line == NULL || tokensp == NULL || token_countp == NULL) {
        return QUERY_ERROR;
    }
    *tokensp = NULL;
    *token_countp = 0;

    /* Lowercase every letter and reject numbers or punctuation. */
    for (position = 0; line[position] != '\0'; position++) {
        if (isalpha((unsigned char)line[position])) {
            line[position] = (char)tolower((unsigned char)line[position]);
        } else if (!isspace((unsigned char)line[position])) {
            return QUERY_INVALID;
        }
    }

    /* Split at spaces or tabs and store each word in order. */
    token = strtok(line, " \t\r\n\v\f");
    while (token != NULL && status == QUERY_VALID) {
        if (token_count == token_capacity) {
            if (token_capacity > SIZE_MAX / (2 * sizeof(char *))) {
                status = QUERY_ERROR;
            } else {
                token_capacity = token_capacity == 0 ? 8 :
                                 token_capacity * 2;
                larger_tokens = realloc(tokens,
                                        token_capacity * sizeof(char *));
                if (larger_tokens == NULL) {
                    status = QUERY_ERROR;
                } else {
                    tokens = larger_tokens;
                }
            }
        }
        if (status == QUERY_VALID) {
            tokens[token_count] = token;
            token_count++;
            token = strtok(NULL, " \t\r\n\v\f");
        }
    }

    /* Treat a line containing only whitespace as an empty query. */
    if (status == QUERY_VALID && token_count == 0) {
        status = QUERY_EMPTY;
    }

    /* Require every AND or OR to sit between two query words. */
    if (status == QUERY_VALID &&
        (is_operator(tokens[0]) || is_operator(tokens[token_count - 1]))) {
        status = QUERY_INVALID;
    }
    position = 1;
    while (status == QUERY_VALID && position < token_count) {
        if (is_operator(tokens[position - 1]) &&
            is_operator(tokens[position])) {
            status = QUERY_INVALID;
        }
        position++;
    }

    /* Give valid query words to the caller; otherwise free the unused word list. */
    if (status == QUERY_VALID) {
        *tokensp = tokens;
        *token_countp = token_count;
    } else {
        free(tokens);
    }

    return status;
}

/* Print the accepted query on screen in lowercase. */
static void print_normalized_query(char **tokens, size_t token_count)
{
    size_t position;

    /* Add one space between words without adding a leading space. */
    for (position = 0; position < token_count; position++) {
        if (position > 0) {
            putchar(' ');
        }
        fputs(tokens[position], stdout);
    }
    putchar('\n');
}

/* Let the hash search compare an indexed word in the file with a query word. */
static bool word_matches(void *elementp, const void *searchkeyp)
{
    index_word_t *entry = elementp;
    const char *word = searchkeyp;

    return entry != NULL && entry->word != NULL && word != NULL &&
           strcmp(entry->word, word) == 0;
}

/* Let the queue search compare an indexed page with a page ID. */
static bool document_matches(void *elementp, const void *searchkeyp)
{
    index_document_t *entry = elementp;
    const int *document_idp = searchkeyp;

    return entry != NULL && document_idp != NULL &&
           entry->document_id == *document_idp;
}

/* Return how many times a word occurs on one page, or zero if absent. */
static int word_count(index_t *index, const char *word, int document_id)
{
    index_word_t *word_entry = NULL;
    index_document_t *document_entry;
    size_t word_length;
    int count = 0;

    /* Find the word in the index file, then get its count for the requested page. */
    if (index != NULL && word != NULL && document_id > 0) {
        word_length = strlen(word);
        if (word_length <= INT32_MAX) {
            word_entry = hsearch(index, word_matches, word,
                                 (int32_t)word_length);
        }
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

/* Add an OR group's rank to the total without allowing integer overflow. */
static uint64_t add_group_rank(uint64_t total, uint64_t group_rank)
{
    if (UINT64_MAX - total < group_rank) {
        return UINT64_MAX;
    }
    return total + group_rank;
}

/* Rank one page (take the minimum for AND and add the results for OR). */
static uint64_t document_rank(index_t *index, char **tokens,
                              size_t token_count, int document_id)
{
    uint64_t total_rank = 0;
    uint64_t group_rank = 0;
    int count;
    size_t position;
    bool group_has_word = false;

    /* Skip the AND operator and short words; use OR to finish each AND group. */
    for (position = 0; position < token_count; position++) {
        if (strcmp(tokens[position], "or") == 0) {
            if (group_has_word) {
                total_rank = add_group_rank(total_rank, group_rank);
            }
            group_rank = 0;
            group_has_word = false;
        } else if (strcmp(tokens[position], "and") != 0 &&
                   strlen(tokens[position]) >= 3) {
            count = word_count(index, tokens[position], document_id);
            if (!group_has_word || (uint64_t)count < group_rank) {
                group_rank = (uint64_t)count;
            }
            group_has_word = true;
        }
    }

    /* Add the final AND group after all query words have been processed. */
    if (group_has_word) {
        total_rank = add_group_rank(total_rank, group_rank);
    }

    return total_rank;
}

/* Put higher ranks first and lower page IDs first when ranks are equal. */
static int compare_results(const void *firstp, const void *secondp)
{
    const query_result_t *first = firstp;
    const query_result_t *second = secondp;

    if (first->rank > second->rank) {
        return -1;
    }
    if (first->rank < second->rank) {
        return 1;
    }
    if (first->document_id < second->document_id) {
        return -1;
    }
    if (first->document_id > second->document_id) {
        return 1;
    }
    return 0;
}

/* Free the result queue and the array containing each page ID and rank. */
static void ranked_documents_delete(ranked_documents_t *ranked)
{
    if (ranked != NULL) {
        qclose(ranked->documents);
        free(ranked->storage);
        ranked->documents = NULL;
        ranked->storage = NULL;
    }
}

/* Score every page, record ranks above zero, sort the results, and queue them for output. */
static bool rank_documents(index_t *index, char **tokens, size_t token_count,
                           int last_document_id,
                           ranked_documents_t *ranked)
{
    query_result_t *larger_storage;
    size_t result_capacity = 0;
    size_t result_count = 0;
    size_t position;
    int document_id;
    uint64_t rank;
    bool valid = false;

    /* Stop if the index, query words, page range, or result destination is missing. */
    if (index == NULL || tokens == NULL || token_count == 0 ||
        last_document_id <= 0 || ranked == NULL) {
        return valid;
    }

    /* Start with no result array or queue so allocation and cleanup are safe. */
    ranked->storage = NULL;
    ranked->documents = NULL;

    /* Score page IDs from 1 through the last ID and only keep ranks above zero. */
    document_id = 1;
    while (document_id <= last_document_id) {
        rank = document_rank(index, tokens, token_count, document_id);
        if (rank > 0) {
            if (result_count == result_capacity) {
                if (result_capacity >
                    SIZE_MAX / (2 * sizeof(query_result_t))) {
                    ranked_documents_delete(ranked);
                    return valid;
                }
                result_capacity = result_capacity == 0 ? 16 :
                                  result_capacity * 2;
                larger_storage = realloc(
                    ranked->storage,
                    result_capacity * sizeof(query_result_t));
                if (larger_storage == NULL) {
                    ranked_documents_delete(ranked);
                    return valid;
                }
                ranked->storage = larger_storage;
            }
            ranked->storage[result_count].document_id = document_id;
            ranked->storage[result_count].rank = rank;
            result_count++;
        }
        if (document_id == last_document_id) {
            break;
        }
        document_id++;
    }

    /* Sort the saved matches before placing them in a queue. */
    if (result_count > 1) {
        qsort(ranked->storage, result_count, sizeof(query_result_t),
              compare_results);
    }

    ranked->documents = qopen();
    if (ranked->documents == NULL) {
        ranked_documents_delete(ranked);
        return valid;
    }

    /* Add sorted results to the queue so qget returns the highest rank first. */
    valid = true;
    for (position = 0; position < result_count && valid; position++) {
        if (qput(ranked->documents, &ranked->storage[position]) != 0) {
            valid = false;
        }
    }
    if (!valid) {
        ranked_documents_delete(ranked);
    }

    return valid;
}

/* Print each match's rank, page ID, and URL from its saved crawler page. */
static bool print_ranked_documents(ranked_documents_t *ranked,
                                   const char *pagedir)
{
    query_result_t *result;
    webpage_t *page;
    bool valid = true;

    /* Remove each queued result, print its page information, and free the page. */
    result = qget(ranked->documents);
    while (result != NULL) {
        page = pageload(result->document_id, (char *)pagedir);
        if (page == NULL) {
            fprintf(stderr, "query: skipping unavailable page %d\n",
                    result->document_id);
        } else {
            if (printf("rank: %" PRIu64 ": doc: %d : %s\n",
                       result->rank, result->document_id,
                       webpage_getURL(page)) < 0) {
                valid = false;
            }
            webpage_delete(page);
        }
        result = qget(ranked->documents);
    }

    return valid;
}

/* Check that the page path is an accessible directory with crawler page 1. */
static page_directory_status_t check_page_directory(const char *pagedir)
{
    struct stat directory_info;
    webpage_t *page;

    /* Reject a missing path, a non-directory, or an inaccessible directory. */
    if (pagedir == NULL || stat(pagedir, &directory_info) != 0 ||
        !S_ISDIR(directory_info.st_mode) ||
        access(pagedir, R_OK | X_OK) != 0) {
        return PAGE_DIRECTORY_INACCESSIBLE;
    }

    /* Loading page 1 confirms that this directory contains crawler output. */
    page = pageload(1, (char *)pagedir);
    if (page == NULL) {
        return PAGE_DIRECTORY_NOT_CRAWLED;
    }
    webpage_delete(page);

    return PAGE_DIRECTORY_VALID;
}

/* Check that the index path identifies a readable file. */
static bool index_file_is_readable(const char *indexnm)
{
    struct stat file_info;

    return indexnm != NULL && stat(indexnm, &file_info) == 0 &&
           S_ISREG(file_info.st_mode) && access(indexnm, R_OK) == 0;
}

/* Find the highest page number in the crawler directory. */
static int find_last_document_id(const char *pagedir)
{
    DIR *directory;
    struct dirent *entry;
    int document_id;
    int last_document_id = 0;

    /* Read crawler filenames as page IDs and remember the highest ID. */
    directory = opendir(pagedir);
    if (directory != NULL) {
        entry = readdir(directory);
        while (entry != NULL) {
            document_id = atoi(entry->d_name);
            if (document_id > last_document_id) {
                last_document_id = document_id;
            }
            entry = readdir(directory);
        }
        if (closedir(directory) != 0) {
            last_document_id = 0;
        }
    }

    return last_document_id;
}

/* Read, validate, and answer one query at a time until EOF. */
static int32_t process_queries(index_t *index, const char *pagedir,
                               int last_document_id, bool quiet)
{
    char *line;
    char **tokens;
    size_t token_count;
    query_status_t query_status;
    ranked_documents_t ranked;
    bool read_error = false;
    int32_t status = 0;

    /* Show the interactive prompt unless -q requested quiet input. */
    while (status == 0) {
        if (!quiet) {
            fputs("> ", stdout);
            fflush(stdout);
        }

        line = read_query_line(stdin, &read_error);
        if (line == NULL) {
            if (read_error) {
                fprintf(stderr, "query: could not read the query\n");
                status = 1;
            }
            break;
        }

        /* Ignore empty lines, reject invalid lines, and process valid lines. */
        tokens = NULL;
        token_count = 0;
        query_status = parse_query(line, &tokens, &token_count);
        if (query_status == QUERY_INVALID) {
            puts("[invalid query]");
        } else if (query_status == QUERY_ERROR) {
            fprintf(stderr, "query: could not process the query\n");
            status = 1;
        } else if (query_status == QUERY_VALID) {
            /* Echo the valid query, calculate its matches, and print them. */
            print_normalized_query(tokens, token_count);
            if (!rank_documents(index, tokens, token_count,
                                last_document_id, &ranked)) {
                fprintf(stderr, "query: could not rank the query\n");
                status = 1;
            } else {
                if (!print_ranked_documents(&ranked, pagedir)) {
                    status = 1;
                }
                ranked_documents_delete(&ranked);
            }
        }

        /* Free the current query before reading the next line. */
        free(tokens);
        free(line);
    }

    return status;
}

/* Validate the files, load the index, process queries, and free the index. */
int main(int argc, char *argv[])
{
    const char *pagedir;
    const char *indexnm;
    page_directory_status_t directory_status;
    index_t *index = NULL;
    int last_document_id;
    int32_t status = 1;
    bool quiet = false;

    /* Accept exactly two paths and an optional -q flag. */
    if (argc != 3 && argc != 4) {
        usage();
        return EXIT_FAILURE;
    }
    if (argc == 4) {
        if (strcmp(argv[3], "-q") != 0) {
            usage();
            return EXIT_FAILURE;
        }
        quiet = true;
    }
    pagedir = argv[1];
    indexnm = argv[2];

    /* Reject an inaccessible directory or one without crawler output. */
    directory_status = check_page_directory(pagedir);
    if (directory_status == PAGE_DIRECTORY_INACCESSIBLE) {
        fprintf(stderr, "query: page directory is not accessible\n");
        return EXIT_FAILURE;
    }
    if (directory_status == PAGE_DIRECTORY_NOT_CRAWLED) {
        fprintf(stderr, "query: directory does not contain crawler pages\n");
        return EXIT_FAILURE;
    }

    /* Reject an unreadable or malformed index before processing queries. */
    if (!index_file_is_readable(indexnm)) {
        fprintf(stderr, "query: index file is not readable\n");
        return EXIT_FAILURE;
    }
    index = indexload(indexnm);
    if (index == NULL) {
        fprintf(stderr, "query: index file has invalid contents\n");
        return EXIT_FAILURE;
    }

    /* Find the page-number range, then read and answer queries. */
    last_document_id = find_last_document_id(pagedir);
    if (last_document_id <= 0) {
        fprintf(stderr, "query: could not inspect the page directory\n");
    } else {
        status = process_queries(index, pagedir, last_document_id, quiet);
    }

    /* Free all index memory before exiting. */
    indexdelete(index);

    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
