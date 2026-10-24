/*
 * crawler.c -- crawl internal webpages to a fixed depth and save each page.
 *
 * The program validates its command line, processes webpages in queue order,
 * uses a hash table to prevent duplicate URLs, and saves successful fetches as
 * consecutively numbered files.
 */
#include "crawlerlib.h"
#include "hash.h"
#include "pageio.h"
#include "queue.h"
#include "webpage.h"

#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Start the visited table with 64 buckets; the hash module may resize it. */
#define VISITED_BUCKETS 64

/* Print the exact command syntax accepted by main. */
static void usage(void)
{
    fprintf(stderr, "usage: crawler <seedurl> <pagedir> <maxdepth>\n");
}

/* Allocate a separately owned copy of one null-terminated string. */
static char *copy_string(const char *source)
{
    char *copy = NULL;
    size_t length;

    /* Include the terminating null byte in both the allocation and the copy. */
    if (source != NULL) {
        length = strlen(source) + 1;
        copy = malloc(length);
        if (copy != NULL) {
            memcpy(copy, source, length);
        }
    }

    return copy;
}

/* Compare a URL stored in the hash table with a requested URL. */
static bool url_matches(void *elementp, const void *searchkeyp)
{
    const char *stored_url = elementp;
    const char *requested_url = searchkeyp;
    bool matches = false;

    /* Compare the strings only when both URL pointers are valid. */
    if (stored_url != NULL && requested_url != NULL &&
        strcmp(stored_url, requested_url) == 0) {
        matches = true;
    }

    return matches;
}

/* Parse maxdepth only when the entire argument is a non-negative int. */
static bool parse_depth(const char *text, int *depthp)
{
    char *end = NULL;
    long value;
    bool valid = false;

    /* Reject missing pointers and an empty argument before calling strtol. */
    if (text != NULL && depthp != NULL && *text != '\0') {
        errno = 0;
        value = strtol(text, &end, 10);

        /* Accept only an in-range conversion that consumed every character. */
        if (errno == 0 && *end == '\0' && value >= 0 && value <= INT_MAX) {
            *depthp = (int)value;
            valid = true;
        }
    }

    return valid;
}

/* Delete every queued webpage and then release the empty queue itself. */
static void delete_page_queue(queue_t *pages)
{
    webpage_t *page;

    /* qget transfers each webpage to this function for complete deletion. */
    if (pages != NULL) {
        page = qget(pages);
        while (page != NULL) {
            webpage_delete(page);
            page = qget(pages);
        }
        qclose(pages);
    }
}

/*
 * queue_internal_links -- queue unseen internal links at the next depth.
 *
 * Each URL returned by webpage_getNextURL is either transferred to the visited
 * table or freed here. Each new webpage is either transferred to the queue or
 * deleted here. Returns 0 after a complete scan and non-zero after a parser,
 * allocation, hash-table, or queue failure.
 */
static int32_t queue_internal_links(webpage_t *page, queue_t *pages,
                                    hashtable_t *visited)
{
    webpage_t *new_page;
    char *removed_url;
    char *url = NULL;
    int key_length;
    int position = 0;
    int next_position = 0;
    int32_t status = 0;

    /* Extract the first allocated URL and remember where scanning should resume. */
    next_position = webpage_getNextURL(page, position, &url);
    while (status == 0 && next_position > 0) {
        position = next_position;

        /* Normalize the URL and reject external or invalid links. */
        if (IsInternalURL(url)) {
            key_length = (int)strlen(url);

            /* Continue only when this internal URL has not been visited. */
            if (hsearch(visited, url_matches, url, key_length) == NULL) {
                /* Give the new page a depth one greater than its parent page. */
                new_page = webpage_new(url, webpage_getDepth(page) + 1, NULL);
                if (new_page == NULL) {
                    /* Keep ownership of url; the common cleanup below frees it. */
                    status = 1;
                } else if (hput(visited, url, url, key_length) != 0) {
                    /* Hash insertion failed, so the new page was never queued. */
                    webpage_delete(new_page);
                    status = 1;
                } else if (qput(pages, new_page) != 0) {
                    /* Undo the hash insertion when the page cannot be queued. */
                    removed_url = hremove(visited, url_matches, url, key_length);
                    free(removed_url);
                    webpage_delete(new_page);

                    /* removed_url and url identify the same allocation. */
                    url = NULL;
                    status = 1;
                } else {
                    /* Transfer url to the hash table and new_page to the queue. */
                    url = NULL;
                }
            }
        }

        /* Free any external, invalid, duplicate, or failed-insertion URL. */
        free(url);
        url = NULL;

        /* Extract the next URL only while all earlier processing has succeeded. */
        if (status == 0) {
            next_position = webpage_getNextURL(page, position, &url);
        }
    }

    /* Treat the parser's allocation-error code as a fatal scanning failure. */
    if (status == 0 && next_position == -2) {
        status = 1;
    }

    /* Release a URL returned immediately before any scanning failure. */
    free(url);

    return status;
}

/*
 * crawl -- perform a breadth-first crawl and save each successfully fetched page.
 *
 * Unavailable remote pages are skipped. Setup, allocation, queue, scanning, and
 * file-writing failures stop the crawl. Returns 0 only after all queued work is
 * processed successfully.
 */
static int32_t crawl(const char *seed, char *pagedir, int maxdepth)
{
    hashtable_t *visited = NULL;
    queue_t *pages = NULL;
    webpage_t *page = NULL;
    webpage_t *seed_page = NULL;
    char *seed_copy = NULL;
    int document_id = 0;
    int32_t status = 1;

    /* Copy the seed because URL normalization modifies its input in place. */
    seed_copy = copy_string(seed);

    /* Normalize the copied seed and reject it when it is not internal. */
    if (seed_copy != NULL && !IsInternalURL(seed_copy)) {
        fprintf(stderr, "crawler: seed URL must be internal\n");
        free(seed_copy);
        seed_copy = NULL;
    }

    /* Create the queue of pending pages and the set of visited URLs. */
    pages = qopen();
    visited = hopen(VISITED_BUCKETS);

    /* Create the depth-zero seed webpage only after setup succeeds. */
    if (seed_copy != NULL && pages != NULL && visited != NULL) {
        seed_page = webpage_new(seed_copy, 0, NULL);
    }

    /* Mark the seed visited before allowing it into the work queue. */
    if (seed_page != NULL &&
        hput(visited, seed_copy, seed_copy, (int)strlen(seed_copy)) == 0) {
        /* The visited table now owns seed_copy. */
        seed_copy = NULL;
        if (qput(pages, seed_page) == 0) {
            /* The queue now owns seed_page, and initialization is complete. */
            seed_page = NULL;
            status = 0;
        }
    }

    /* Take ownership of the first queued page and process pages in FIFO order. */
    page = qget(pages);
    while (status == 0 && page != NULL) {
        /* Save only pages whose remote HTML was fetched successfully. */
        if (webpage_fetch(page)) {
            /* Assign consecutive IDs only to successfully fetched pages. */
            document_id++;
            if (pagesave(page, document_id, pagedir) != 0) {
                /* A local file-writing failure stops the crawl. */
                fprintf(stderr, "crawler: could not save page %d\n", document_id);
                status = 1;
            } else {
                /* Report the file ID, normalized URL, and crawl depth saved. */
                printf("saved %d: %s (depth %d)\n", document_id,
                       webpage_getURL(page), webpage_getDepth(page));
            }

            /* Queue unseen internal links only below the requested depth limit. */
            if (status == 0 && webpage_getDepth(page) < maxdepth &&
                queue_internal_links(page, pages, visited) != 0) {
                fprintf(stderr, "crawler: could not queue an internal URL\n");
                status = 1;
            }
        } else {
            /* An unavailable page is nonfatal, receives no ID, and is not saved. */
            fprintf(stderr, "ignored unavailable URL: %s\n",
                    webpage_getURL(page));
        }

        /* Delete the processed page, including its URL and fetched HTML. */
        webpage_delete(page);
        page = NULL;

        /* Stop dequeuing after a fatal error; otherwise take the next page. */
        if (status == 0) {
            page = qget(pages);
        }
    }

    /* Release every resource still owned locally after success or failure. */
    free(seed_copy);
    webpage_delete(seed_page);
    webpage_delete(page);
    delete_page_queue(pages);
    happly(visited, free);
    hclose(visited);

    /* Print the final count only after a complete successful crawl. */
    if (status == 0) {
        printf("crawl complete: %d pages saved\n", document_id);
    }

    return status;
}

int main(int argc, char *argv[])
{
    int maxdepth = 0;
    int32_t status = 1;

    /* Reject any command line that does not contain exactly three arguments. */
    if (argc != 4) {
        usage();
    } else if (!pagedir_is_valid(argv[2])) {
        /* Require an existing directory with write and search permission. */
        fprintf(stderr, "crawler: pagedir must be a writable directory\n");
        usage();
    } else if (!parse_depth(argv[3], &maxdepth)) {
        /* Require maxdepth to be a complete non-negative decimal integer. */
        fprintf(stderr, "crawler: maxdepth must be a non-negative integer\n");
        usage();
    } else {
        /* Start the crawl only after all command-line values are valid. */
        status = crawl(argv[1], argv[2], maxdepth);
    }

    /* Convert the crawler's internal status into the process exit status. */
    return status == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
