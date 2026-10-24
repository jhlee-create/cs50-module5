/*
 * crawlerlib.c -- shared support for validating crawler output directories.
 */
#include "crawlerlib.h"

#include <sys/stat.h>
#include <unistd.h>

/*
 * pagedir_is_valid -- check that a page directory can receive saved pages.
 *
 * Returns true only when dirname exists, names a directory, and allows the
 * crawler to enter it and create files there.
 */
bool pagedir_is_valid(const char *dirname)
{
    struct stat directory_info;
    bool valid = false;

    /*
     * stat confirms that the path exists and identifies its file type.
     * access confirms that the crawler may create files inside the directory.
     */
    if (dirname != NULL && stat(dirname, &directory_info) == 0 &&
        S_ISDIR(directory_info.st_mode) &&
        access(dirname, W_OK | X_OK) == 0) {
        valid = true;
    }

    /* Missing paths, ordinary files, and unusable directories return false. */
    return valid;
}
