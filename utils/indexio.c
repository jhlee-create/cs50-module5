/* Save, load, and free every word and page count in the index. */
#include "indexio.h"

#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Keep the output file and its status available while each entry is written. */
static FILE *save_output = NULL;
static int32_t save_status = 1;

/* Copy the word because the file line is freed after it is read. */
static char *copy_word(const char *word)
{
    char *copy = NULL;
    size_t length;

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

/* Compare word text so the requested word can be found in the loaded index. */
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

/* Free one word and every page count that belongs to it. */
static void delete_word(void *elementp)
{
    index_word_t *entry = elementp;
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

/* Free every word before freeing the complete index that contains them. */
void indexdelete(index_t *index)
{
    /* Skip cleanup when there is no index to free. */
    if (index != NULL) {
        happly(index, delete_word);
        hclose(index);
    }
}

/* Read one full index line so its values can be separated and loaded. */
static char *read_line(FILE *input, bool *errorp)
{
    char *line = NULL;
    char *larger_line;
    size_t capacity = 128;
    size_t length = 0;
    int character = EOF;

    /* Start small because the line length is unknown. */
    if (errorp != NULL) {
        *errorp = false;
    }
    if (input != NULL && errorp != NULL) {
        line = malloc(capacity);
        if (line == NULL) {
            *errorp = true;
        }
    }

    /* Stop at the newline because it separates this word from the next one. */
    while (line != NULL && (character = fgetc(input)) != '\n' &&
           character != EOF) {
        if (length + 1 >= capacity) {
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

/* Check saved words so only the indexer's accepted word format is loaded. */
static bool valid_saved_word(const char *word)
{
    size_t position = 0;
    size_t length = 0;
    bool valid = false;

    /* Reject missing and short words because accepted words have 3+ letters. */
    if (word != NULL) {
        length = strlen(word);
        if (length >= 3) {
            valid = true;
        }
    }

    /* Reject other characters so loaded words match the saved format. */
    while (valid && position < length) {
        if (!isalpha((unsigned char)word[position]) ||
            !islower((unsigned char)word[position])) {
            valid = false;
        }
        position++;
    }

    return valid;
}

/* Convert page IDs and counts from file text into positive whole numbers. */
static bool parse_positive_integer(const char *token, int *valuep)
{
    char *end = NULL;
    long value;
    bool valid = false;

    /* Reject values that cannot be valid page IDs or counts. */
    if (token != NULL && valuep != NULL && *token != '\0') {
        errno = 0;
        value = strtol(token, &end, 10);
        if (errno == 0 && *end == '\0' && value > 0 && value <= INT_MAX) {
            *valuep = (int)value;
            valid = true;
        }
    }

    return valid;
}

/* Rebuild one saved word, page ID, and count in the loaded index. */
static int32_t load_count(index_t *index, const char *word,
                          int document_id, int count)
{
    index_word_t *word_entry;
    index_document_t *document_entry;
    int32_t status = 1;
    bool new_word = false;

    /* Reject incomplete values so invalid data is not added to the index. */
    if (index == NULL || word == NULL || *word == '\0' ||
        document_id <= 0 || count <= 0) {
        return status;
    }

    /* Find the word first so its page counts can be updated. */
    word_entry = hsearch(index, word_matches, word, (int32_t)strlen(word));

    /* Create a word and page list when this is its first saved count. */
    if (word_entry == NULL) {
        word_entry = calloc(1, sizeof(index_word_t));
        if (word_entry != NULL) {
            word_entry->word = copy_word(word);
            word_entry->documents = qopen();
        }
        if (word_entry == NULL || word_entry->word == NULL ||
            word_entry->documents == NULL) {
            delete_word(word_entry);
            return status;
        }
        new_word = true;
    }

    /* Reuse the page count when present; otherwise add the saved page count. */
    document_entry = qsearch(word_entry->documents, document_matches,
                             &document_id);
    if (document_entry != NULL) {
        if (document_entry->count <= INT_MAX - count) {
            document_entry->count += count;
            status = 0;
        }
    } else {
        document_entry = malloc(sizeof(index_document_t));
        if (document_entry != NULL) {
            document_entry->document_id = document_id;
            document_entry->count = count;
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
        delete_word(word_entry);
    }

    return status;
}

/* Separate one index line into its word, page IDs, and counts. */
static bool load_index_line(index_t *index, char *line)
{
    char *word;
    char *document_token;
    char *count_token;
    int document_id;
    int count;
    bool valid = false;

    /* Check the first value because every line must begin with a valid word. */
    word = strtok(line, " \t\r\n");
    if (!valid_saved_word(word)) {
        return valid;
    }

    /* Read values in pairs so every page ID has one matching count. */
    document_token = strtok(NULL, " \t\r\n");
    while (document_token != NULL) {
        count_token = strtok(NULL, " \t\r\n");
        if (!parse_positive_integer(document_token, &document_id) ||
            !parse_positive_integer(count_token, &count) ||
            load_count(index, word, document_id, count) != 0) {
            return false;
        }
        valid = true;
        document_token = strtok(NULL, " \t\r\n");
    }

    return valid;
}

/* Add one page ID and count to the current word's output line. */
static void save_document(void *elementp)
{
    index_document_t *document = elementp;

    /* Keep the first write error so the complete save can report failure. */
    if (document != NULL && save_status == 0 &&
        fprintf(save_output, " %d %d", document->document_id,
                document->count) < 0) {
        save_status = 1;
    }
}

/* Write one complete line for one word and all of its page counts. */
static void save_word(void *elementp)
{
    index_word_t *word = elementp;

    /* Write the word first so the remaining numbers belong to it. */
    if (word != NULL && save_status == 0 &&
        fprintf(save_output, "%s", word->word) < 0) {
        save_status = 1;
    }

    /* Add every page ID and count stored for this word. */
    if (word != NULL && save_status == 0) {
        qapply(word->documents, save_document);
    }

    /* End the line so the next word starts on a separate line. */
    if (word != NULL && save_status == 0 && fputc('\n', save_output) == EOF) {
        save_status = 1;
    }
}

/* Save the complete in-memory index to the requested text file. */
int32_t indexsave(index_t *index, const char *indexnm)
{
    FILE *output = NULL;
    bool file_opened = false;

    /* Reset earlier save information before opening a new file. */
    save_output = NULL;
    save_status = 1;

    /* Open the output only when both the index and filename are usable. */
    if (index != NULL && indexnm != NULL && *indexnm != '\0') {
        output = fopen(indexnm, "w");
        if (output != NULL) {
            file_opened = true;
            save_output = output;
            save_status = 0;
        }
    }

    /* Visit every word so none are missing from the saved index. */
    if (save_status == 0) {
        happly(index, save_word);
    }

    /* Check closing because buffered output may fail at that point. */
    if (output != NULL && fclose(output) != 0) {
        save_status = 1;
    }

    /* Delete a partial file so it cannot be loaded as a complete index. */
    if (save_status != 0 && file_opened) {
        (void)remove(indexnm);
    }

    save_output = NULL;
    return save_status;
}

/* Recreate the in-memory index from a saved text file. */
index_t *indexload(const char *indexnm)
{
    FILE *input = NULL;
    index_t *index = NULL;
    char *line = NULL;
    bool valid = true;
    bool read_failed = false;

    /* Create an empty index only after the input file opens successfully. */
    if (indexnm != NULL && *indexnm != '\0') {
        input = fopen(indexnm, "r");
    }
    if (input != NULL) {
        index = hopen(INDEX_BUCKETS);
    }

    /* Load one complete word and its page counts from each line. */
    while (index != NULL && valid &&
           (line = read_line(input, &read_failed)) != NULL) {
        valid = load_index_line(index, line);
        free(line);
        line = NULL;
    }

    /* Reject the load if the file could not be completely read or closed. */
    if (read_failed || (input != NULL && ferror(input))) {
        valid = false;
    }
    if (input != NULL && fclose(input) != 0) {
        valid = false;
    }

    /* Free partial data so a failed load never returns an incomplete index. */
    if (!valid) {
        indexdelete(index);
        index = NULL;
    }

    return index;
}
