/* Save numbered webpage files so they can be loaded again later. */
#include "pageio.h"

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Combine the directory and page ID so the correct file can be opened. */
static char *make_page_path(const char *dirnm, int id)
{
    char *path = NULL;
    int path_length;

    /* Measure the complete path so exactly enough memory is allocated. */
    if (dirnm != NULL && id > 0) {
        path_length = snprintf(NULL, 0, "%s/%d", dirnm, id);
        if (path_length >= 0) {
            path = malloc((size_t)path_length + 1);
        }

        /* Build the path only after enough memory was allocated. */
        if (path != NULL) {
            (void)snprintf(path, (size_t)path_length + 1, "%s/%d", dirnm, id);
        }
    }

    return path;
}

/* Read one full line so its text can be used without the newline. */
static char *read_line(FILE *input)
{
    char *line = NULL;
    char *larger_line;
    size_t capacity = 128;
    size_t length = 0;
    int character = EOF;

    /* Start small because the line length is unknown. */
    if (input != NULL) {
        line = malloc(capacity);
    }

    /* Stop at the newline because it separates this value from the next one. */
    while (line != NULL && (character = fgetc(input)) != '\n' &&
           character != EOF) {
        if (length + 1 >= capacity) {
            capacity *= 2;
            larger_line = realloc(line, capacity);
            if (larger_line == NULL) {
                free(line);
                line = NULL;
            } else {
                line = larger_line;
            }
        }

        /* Grow the memory when a long line does not fit. */
        if (line != NULL) {
            line[length] = (char)character;
            length++;
        }
    }

    /* Return NULL when no characters remain to form another line. */
    if (line != NULL && character == EOF && length == 0) {
        free(line);
        line = NULL;
    }

    /* Add '\0' so the returned characters form a complete C string. */
    if (line != NULL) {
        line[length] = '\0';
    }

    return line;
}

/* Read the depth or HTML length and reject extra text on that line. */
static bool read_integer_line(FILE *input, int *valuep)
{
    int line_end;
    bool valid = false;

    /* Check the next character so only a number appears on the line. */
    if (input != NULL && valuep != NULL && fscanf(input, "%d", valuep) == 1) {
        line_end = fgetc(input);
        /* Accept either common line ending so files from both systems work. */
        if (line_end == '\n') {
            valid = true;
        } else if (line_end == '\r' && fgetc(input) == '\n') {
            valid = true;
        }
    }

    return valid;
}

/* Save one webpage so the indexer can load it by page ID. */
int32_t pagesave(webpage_t *pagep, int id, char *dirnm)
{
    char *path = NULL;
    char *url;
    char *html;
    FILE *page_file = NULL;
    int html_length;
    int32_t status = 1;
    bool file_opened = false;

    /* Stop when the webpage cannot be connected to a valid output file. */
    if (pagep == NULL || id <= 0 || dirnm == NULL) {
        return status;
    }

    /* Read the webpage values needed to reproduce the page later. */
    url = webpage_getURL(pagep);
    html = webpage_getHTML(pagep);
    html_length = webpage_getHTMLlen(pagep);

    /* Do not create a file when required webpage data is missing. */
    if (url != NULL && html != NULL && html_length >= 0) {
        path = make_page_path(dirnm, id);
    }

    /* Replace an older file so one page ID has only one saved webpage. */
    if (path != NULL) {
        page_file = fopen(path, "wb");
        if (page_file != NULL) {
            file_opened = true;
        }
    }

    /* Write the URL, depth, HTML length, and then the HTML content. */
    if (page_file != NULL &&
        fprintf(page_file, "%s\n%d\n%d\n", url,
                webpage_getDepth(pagep), html_length) >= 0 &&
        fwrite(html, 1, (size_t)html_length, page_file) ==
            (size_t)html_length) {
        status = 0;
    }

    /* Check closing because buffered output may fail at that point. */
    if (page_file != NULL && fclose(page_file) != 0) {
        status = 1;
    }

    /* Delete a partial file so it cannot be mistaken for a complete page. */
    if (status != 0 && file_opened) {
        (void)remove(path);
    }

    /* Free the path because it is no longer needed after saving. */
    free(path);

    return status;
}

/* Rebuild one webpage from its numbered file. */
webpage_t *pageload(int id, char *dirnm)
{
    char *path = NULL;
    char *url = NULL;
    char *html = NULL;
    FILE *page_file = NULL;
    webpage_t *page = NULL;
    int depth = -1;
    int html_length = -1;
    size_t bytes_read;

    /* Use the directory and page ID to open the correct file. */
    path = make_page_path(dirnm, id);
    if (path != NULL) {
        page_file = fopen(path, "rb");
    }

    /* Read the first line because it contains the webpage URL. */
    if (page_file != NULL) {
        url = read_line(page_file);
    }

    /* Read the next two lines because they contain depth and HTML length. */
    if (url != NULL && *url != '\0' &&
        read_integer_line(page_file, &depth) && depth >= 0 &&
        read_integer_line(page_file, &html_length) && html_length >= 0) {
        html = malloc((size_t)html_length + 1);
    }

    /* Use the saved length so the complete HTML is read. */
    if (html != NULL) {
        bytes_read = fread(html, 1, (size_t)html_length, page_file);
        if (bytes_read == (size_t)html_length) {
            html[html_length] = '\0';
            page = webpage_new(url, depth, html);
        }
    }

    /* Keep the loaded HTML in the webpage after the function returns. */
    if (page != NULL) {
        html = NULL;
    }

    /* Free the path, URL, and unused HTML after creating the webpage. */
    if (page_file != NULL) {
        (void)fclose(page_file);
    }
    free(html);
    free(url);
    free(path);

    return page;
}
