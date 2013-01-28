#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include <sys/stat.h>

int get_dir_of(char *filename, char *containing_dir) {
    size_t n;
    if (!realpath(filename, containing_dir)) {
        return -1;
    }
    /* chop off trailing filename; dirname() API does not promise to modify
       string in-place on all platforms *shrug* */
    n = strlen(containing_dir);
    while (n > 0 && containing_dir[--n] != '/') ;
    containing_dir[n] = '\0';
    return 0;
}

int find_in_path(char *progname, char *path, char *result, size_t n) {
    struct stat st;
    while (*path != '\0') {
        char *h = result;
        while (*path != ':' && *path != '\0' && (h - result) < n) *h++ = *path++;
        if (*path == ':') path++;
        if ((h - result) >= n - 10) {
            errno = ENAMETOOLONG;
            return -1;
        }
        h[0] = '/';
        h[1] = '\0';
        strncat(h, progname, n - (h - result));
        if (stat(result, &st) == 0) {
            char *buf = strndup(result, n);
            int ret = get_dir_of(buf, result);
            free(buf);
            return ret;
        }
    }
    errno = ENOENT;
    return -1;
}

int main(int argc, char *argv[]) {
    int line;
    char hdist_launch_dir[PATH_MAX]; /* since we use realpath anyway... */

    /* Find hdist_launch_dir */
    if (argv[0][0] == '/') {
        if (get_dir_of(argv[0], hdist_launch_dir) != 0) {
            line = __LINE__;
            goto error;
        }
    } else if (strstr(argv[0], "/")) {
        char buf[PATH_MAX];
        if (!getcwd(buf, PATH_MAX)) {
            line = __LINE__;
            goto error;
        }
        /* the below simple assumes PATH_MAX is long enough */
        strncat(buf, "/", PATH_MAX);
        strncat(buf, argv[0], PATH_MAX);
        get_dir_of(buf, hdist_launch_dir);
    } else {
        /* need to look up in PATH */
        if (find_in_path(argv[0], getenv("PATH"), hdist_launch_dir, PATH_MAX) != 0) {
            line = __LINE__;
            goto error;
        }
    }

    

    return 0;
 error:
    fprintf(stderr, "hdist-launcher.c:%d:%s:", line, strerror(errno));
    return 1;

}
