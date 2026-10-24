/* Save and reload page 1 to confirm that its data does not change. */
#include "pageio.h"
#include "webpage.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Compare two webpages so saving and loading can be checked. */
static bool pages_match(webpage_t *first, webpage_t *second)
{
    int html_length;
    bool matches = false;

    /* Check every value written by pagesave and read by pageload. */
    if (first != NULL && second != NULL &&
        strcmp(webpage_getURL(first), webpage_getURL(second)) == 0 &&
        webpage_getDepth(first) == webpage_getDepth(second) &&
        webpage_getHTMLlen(first) == webpage_getHTMLlen(second)) {
        html_length = webpage_getHTMLlen(first);
        if (memcmp(webpage_getHTML(first), webpage_getHTML(second),
                   (size_t)html_length) == 0) {
            matches = true;
        }
    }

    return matches;
}

int main(int argc, char *argv[])
{
    webpage_t *original = NULL;
    webpage_t *reloaded = NULL;
    int status = EXIT_FAILURE;

    /* Use separate directories so the copy does not replace the original. */
    if (argc != 3) {
        fprintf(stderr, "usage: pageio_test <source-pagedir> <test-pagedir>\n");
        return status;
    }

    /* Load the original page so it can be saved again. */
    original = pageload(1, argv[1]);

    /* Reload the saved copy so it can be compared with the original. */
    if (original != NULL && pagesave(original, 1, argv[2]) == 0) {
        reloaded = pageload(1, argv[2]);
    }

    /* Pass only when saving and loading preserved every page value. */
    if (pages_match(original, reloaded)) {
        printf("PASS pageio: page 1 matched after load, save, and reload\n");
        status = EXIT_SUCCESS;
    } else {
        fprintf(stderr, "FAIL pageio: page 1 changed during load/save/reload\n");
    }

    /* Free both webpages after the comparison is complete. */
    webpage_delete(original);
    webpage_delete(reloaded);

    return status;
}
