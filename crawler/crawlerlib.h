#pragma once
/* crawlerlib.h -- page-directory validation used by the crawler program. */
#include <stdbool.h>

/* Return true when dirname is an existing directory that can receive files. */
bool pagedir_is_valid(const char *dirname);
