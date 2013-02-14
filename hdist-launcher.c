#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>

#include "bsdstring.h"

#define SHEBANG_MAX 1024

static int line; /* error line number for error reporting */
static int debug; /* debug flag */
static const char *debug_header = "hdist-launcher:DEBUG:";

/* Looks up a given program basename in PATH. */
static int find_in_path(char *progname, char *path, char *result, size_t n) {
    while (*path != '\0') {
        char *h = result;
        while (*path != ':' && *path != '\0' && (h - result) < n) *h++ = *path++;
        if (*path == ':') path++;
        if ((h - result) >= n - 10) {
            errno = ENAMETOOLONG;
            return -1;
        }
        if (h == result) {
            /* empty path component; found in current directory, so do not join with "/" */
            h[0] = '\0';
        } else {
            h[0] = '/';
            h[1] = '\0';
        }
        strlcat(result, progname, n);
        if (access(result, X_OK) == 0) return 0;
    }
    errno = ENOENT;
    return -1;
}

/* Since basename and dirname behaviour depends on platform.  Takes an
   absolute path, finds the last '/' and replaces it with 0, and sets
   basename to the trailing part.

   If there is no / in the string, then on return (*basename == path).

*/
static void splitpath(char *path, char **basename) {
    size_t i;
    i = strlen(path);
    while (path[i] != '/' && i > 0) i--;
    if (path[i] != '/') {
        /* no / in string */
        if (basename) *basename = *path;
    } else {
        path[i] = '\0'; /* cut string in two */
        if (basename) *basename = path + i + 1;
    }
}

/* Like readlink, but returns the absolute path name of the link.
   Does zero-terminate the buffer (and returns either 0 or -1). */
static int resolvelink(const char *path, char *buf, size_t n) {
    char *base;
    size_t nread;
    nread = readlink(path, buf, n - 1);
    if (nread == -1) return -1;
    buf[nread] = '\0';
    if (debug) fprintf(stderr, "%sreadlink=%s -> %s\n", debug_header, path, buf);
    if (buf[0] == '/') {
        /* symlink was absolute, we are done */
        return 0;
    } else {
        /* prepend dirname(path) and read the link again */
        strlcpy(buf, path, n);
        splitpath(buf, &base);
        if (base == buf) {
            fprintf(stderr, "%sASSERTION FAILED, LINE %d\n", debug_header, __LINE__);
            return -1;
        }
        base[-1] = '/';
        nread = readlink(path, base, n - (base - buf) - 1);
        if (nread == -1) return -1;
        base[nread] = '\0';
        return 0;
    }
}

static int get_last_link(char *prev, size_t n) {
    char *cur = malloc(n), *next = malloc(n);
    strlcpy(cur, prev, n);
    for (;;) {
        if (resolvelink(cur, next, n) == -1) break;
        strlcpy(prev, cur, n);
        strlcpy(cur, next, n);
    }
    free(cur);
    free(next);
    return (errno == EINVAL) ? 0 : -1;
}

static int get_calling_link(char *argv0, char *calling_link, size_t n) {
    if (strstr(argv0, "/")) {
        strlcpy(calling_link, argv0, n);
        return get_last_link(calling_link, n);
    } else {
        /* need to look up in PATH */
        char buf[PATH_MAX];
        if (find_in_path(argv0, getenv("PATH"), buf, PATH_MAX) != 0) return -1;
        return get_calling_link(buf, calling_link, n);
    }
}

static int get_dir_of(char *filename, char *containing_dir) {
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

/* Fill 'dst' with '${src}${dst}'; return value is same as strlcat  */
static int strlprepend(char *dst, char *src, size_t n) {
    char *buf = malloc(n);
    size_t r;
    r = strlcpy(buf, dst, n);
    if (r >= n) goto error;
    r = strlcpy(dst, src, n);
    if (r >= n) goto error;
    r = strlcat(dst, buf, n);
    if (r >= n) goto error;
    r = 0;
 error:
    free(buf);
    return r;
}

static void skip_whites(char **r) {
    while (**r == ' ' || **r == '\t') ++*r;
}

static void skip_nonwhites(char **r) {
    while (**r != ' ' && **r != '\t' && **r != '\0') ++*r;
}

static int attempt_shebang_launch(char *program_to_launch, char **argv) {
    /* Attempt to open the file to scan for a shebang, which we handle
       ourself.  (If a script is executable but not readable then no
       interpreter can read it anyway; assume such a thing doesn't
       exist.) */
    FILE *f;
    char shebang[SHEBANG_MAX];
    char *r, *interpreter, *arg;
    /* read first line */
    if ((f = fopen(program_to_launch, "r")) == NULL) return -1;
    r = fgets(shebang, SHEBANG_MAX, f);
    fclose(f);
    if (r == NULL) return -1;

    /* Reference: http://homepages.cwi.nl/~aeb/std/hashexclam.html */
    /* TODO: There's a Mac OS X special case on interpreter handling.. */

    /* shebang header */
    if (r[0] != '#' || r[1] != '!') return -1;
    skip_whites(&r);
    if (r == '\0') return -1;
    interpreter = r;
    skip_nonwhites(&r);
    if (*r != '\0') {
        *r = '\0'; /* terminate 'interpreter' string */
        ++r;
        skip_whites(&r);
        /* there may be an argument: */
    }
    
}

int main(int argc, char *argv[]) {
    char calling_link[PATH_MAX];
    char program_to_launch[PATH_MAX];
    size_t n;
    FILE *f = NULL;
    char *s;
    line = 0;

    s = getenv("HDIST_LAUNCHER_DEBUG");
    debug = (s != NULL && strcmp(s, "1") == 0);

    /* Find calling link */
    if (debug) fprintf(stderr, "%sFinding calling link, argv[0]='%s'\n", debug_header, argv[0]);
    if (get_calling_link(argv[0], calling_link, PATH_MAX) != 0) { line = __LINE__; goto error; }
    if (debug) fprintf(stderr, "%scaller=%s\n", debug_header, calling_link);

    /* Open ${calling_link}.link and read the contents */
    if (strlcat(calling_link, ".link", PATH_MAX) >= PATH_MAX) { line = __LINE__; goto error; }
    if ((f = fopen(calling_link, "r")) == NULL) { 
        fprintf(stderr, "hdist-launcher:Cannot open '%s'\n", calling_link);
        return 2;
    }
    if (fgets(program_to_launch, PATH_MAX, f) == NULL) { line = __LINE__; goto error; }
    fclose(f);
    f = NULL;
    n = strlen(program_to_launch);
    if (program_to_launch[n - 1] == '\n') {
        program_to_launch[n - 1] = '\0';
    }

    /* If program_to_launch is relative, it is relative to calling link */
    if (program_to_launch[0] != '/') {
        splitpath(calling_link, &s);
        s[-1] = '/';
        s[0] = '\0';
        strlprepend(program_to_launch, calling_link, PATH_MAX);
    }

    attempt_shebang_launch(program_to_launch, argv);
    execv(program_to_launch, argv);
    fprintf(stderr, "hdist-launcher: Unable to launch '%s' (%s)\n", program_to_launch,
            strerror(errno));
    goto error;

 error:
    if (f != NULL) fclose(f);
    fprintf(stderr, "hdist-launcher.c:%d:%s:\n", line, strerror(errno));
    return 1;

}
