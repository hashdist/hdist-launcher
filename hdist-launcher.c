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
        if (basename) *basename = path;
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

/* 
In the event of prev naming a file, not a link (so that there is no last link),
prev[0] == '\0'.
*/
static int get_last_link(char *prev, size_t n) {
    int no_links = 1;
    char *cur = malloc(n), *next = malloc(n);
    strlcpy(cur, prev, n);
    for (;;) {
        if (resolvelink(cur, next, n) == -1) break;
        no_links = 0;
        strlcpy(prev, cur, n);
        strlcpy(cur, next, n);
    }
    free(cur);
    free(next);
    if (no_links) {
        prev[0] = '\0';
    }
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
    while (**r != ' ' && **r != '\t' && **r != '\n' && **r != '\0') ++*r;
}

static void rstrip(char *s) {
    char *r = s + strlen(s);
    if (s != r) r--;
    while (r >= s && (*r == '\n' || *r == ' ' || *r == '\t')) *r-- = '\0';
}

static int expandvars(char *dst, char *src, char *launchdir, size_t n) {
    char *p;
    while ((p = strstr(src, "${LAUNCHDIR}")) != NULL) {
        size_t m = p - src;
        if (m > n - 1) { errno = ENAMETOOLONG; return -1; }
        strncpy(dst, src, m); /* note: strncpy; does not 0-terminate src */
        dst += m;
        n -= m;

        m = strlen(launchdir);
        strlcpy(dst, launchdir, n);
        dst += m;
        n -= m;

        src = p + strlen("${LAUNCHDIR}");
    }
    strlcpy(dst, src, n);
    return 0;
}

/* return codes: -1 is error; 0 is everything OK but did not find shebang;
   if shebang is found, does an exec */
static int attempt_shebang_launch(char *program_to_launch, int argc, char **argv) {
    /* Attempt to open the file to scan for a shebang, which we handle
       ourself.  (If a script is executable but not readable then no
       interpreter can read it anyway; assume such a thing doesn't
       exist.) */
    FILE *f;
    char shebang[SHEBANG_MAX], interpreter[SHEBANG_MAX], arg[SHEBANG_MAX], launchdir[PATH_MAX];
    char *r, *s, *interpreter_part, *arg_part = NULL, *basename;
    char **new_argv;
    char **p;
    /* read first line */
    if ((f = fopen(program_to_launch, "r")) == NULL) return -1;
    r = fgets(shebang, SHEBANG_MAX, f);
    fclose(f);
    if (r == NULL) return -1;
    /* Reference: http://homepages.cwi.nl/~aeb/std/hashexclam.html */
    /* TODO: There's a Mac OS X special case on interpreter handling.. */

    /* shebang header */
    if (r[0] != '#' || r[1] != '!') return 0; /* shebang not present return code */
    r += 2;
    skip_whites(&r);
    if (r == '\0') return -1;
    interpreter_part = r;
    skip_nonwhites(&r);
    if (*r != '\0') {
        *r = '\0'; /* terminate 'interpreter' string */
        ++r;
        skip_whites(&r);
        /* there may be an argument: */
        arg_part = r;
        rstrip(arg_part);
    }
    strlcpy(launchdir, program_to_launch, PATH_MAX);
    splitpath(launchdir, &basename);
    if (basename == launchdir) {
        fprintf(stderr, "hdist-launcher.c:ASSERTION FAILED:%d\n", __LINE__);
        return -1;
    }
    if (expandvars(interpreter, interpreter_part, launchdir, SHEBANG_MAX) != 0) return -1;
    if (arg_part != NULL) {
        if (expandvars(arg, arg_part, launchdir, SHEBANG_MAX) != 0) return -1;
    } else {
        arg[0] = '\0';
    }
    if (debug) {
        fprintf(stderr, "%sshebang_cmd=%s\n", debug_header, interpreter);
        fprintf(stderr, "%sshebang_arg=%s\n", debug_header, arg);
    }

    /* Done parsing, do launch */
    new_argv = malloc(sizeof(char*) * (argc + 3));
    p = new_argv;
    p++; /* we'll set interpreter below */
    if (arg[0]) *p++ = arg;
    /* substitute argv[0] with program_to_launch */
    argv++;
    *p++ = program_to_launch;
    while (*argv) *p++ = *argv++;
    *p = NULL;
    
    /* The interpreter string may contain : to separate many possible interpreters */
    s = interpreter;
    for (;;) {
        r = strstr(s, ":");
        if (r == NULL) {
            fprintf(stderr, "attempting %s\n", s);
            new_argv[0] = s;
            execv(s, new_argv);
            break;
        }
        r[0] = '\0';
        fprintf(stderr, "attempting %s\n", s);
        new_argv[0] = s;
        execv(s, new_argv);
        s = r + 1;
    }
    /* failed to execute */
    free(new_argv);
    return -1;
}

static int resolve_link_in_textfile(char *filename, char *out, size_t n) {
    FILE *f;
    size_t m;
    char *s;
    if ((f = fopen(filename, "r")) == NULL) { line = __LINE__; return -1; }
    if (fgets(out, n, f) == NULL) { 
        fclose(f);
        errno = ENAMETOOLONG; line = __LINE__;
        return -1;
    }
    fclose(f);
    m = strlen(out);
    if (out[m - 1] == '\n') out[m - 1] = '\0';
    if (out[0] != '/') {
        /* path is relative to file location */
        splitpath(filename, &s);
        if (filename != s) {
            char saved = s[0];
            s[-1] = '/';
            s[0] = '\0';
            if (strlprepend(out, filename, PATH_MAX) >= PATH_MAX) {
                errno = ENAMETOOLONG; line = __LINE__;
                return -1;
            }
            s[0] = saved;
        }
    }
    return 0;
}

static void help() {
    fprintf(stderr, "Usage: You should set up symlinks to hdist-launcher.\n\n");
    fprintf(stderr, "See README on http://github.com/hashdist/hdist-launcher\n");
}

int main(int argc, char *argv[]) {
    char buf[PATH_MAX];
    char calling_link[PATH_MAX];
    char program_to_launch[PATH_MAX];
    char *s;
    line = 0;

    s = getenv("HDIST_LAUNCHER_DEBUG");
    debug = (s != NULL && strcmp(s, "1") == 0);

    /* Find calling link */
    if (debug) fprintf(stderr, "%sFinding calling link, argv[0]='%s'\n", debug_header, argv[0]);
    if (get_calling_link(argv[0], calling_link, PATH_MAX) != 0) { line = __LINE__; goto error; }
    if (calling_link[0] == '\0') {
        help();
        return 0;
    }
    if (debug) fprintf(stderr, "%scaller=%s\n", debug_header, calling_link);

    /* Open ${calling_link}.link and read the contents */
    if (strlcpy(buf, calling_link, PATH_MAX) >= PATH_MAX) { line = __LINE__; goto error; }
    if (strlcat(buf, ".link", PATH_MAX) >= PATH_MAX) { line = __LINE__; goto error; }
    if (resolve_link_in_textfile(buf, program_to_launch, PATH_MAX) != 0) {
        if (errno != ENOENT) goto error;
        /* ${calling_link}.link not found, use ${calling_link}.real */
        if (strlcpy(program_to_launch, calling_link, PATH_MAX) >= PATH_MAX) { line = __LINE__; goto error; }
        if (strlcat(program_to_launch, ".real", PATH_MAX) >= PATH_MAX) { line = __LINE__; goto error; }
    }
    if (debug) fprintf(stderr, "%sprogram=%s\n", debug_header, program_to_launch);
    if (attempt_shebang_launch(program_to_launch, argc, argv) == -1) {
        if (errno == ENOENT) {
            fprintf(stderr, "hdist-launcher:Unable to launch '%s' (%s)", program_to_launch, strerror(errno));
            return 127;
        }
        goto error;
    }
    /* shebang not present */
    execv(program_to_launch, argv);
    fprintf(stderr, "hdist-launcher:Unable to launch '%s' (%s)\n", program_to_launch,
            strerror(errno));
    return 127;
    goto error;

 error:
    fprintf(stderr, "hdist-launcher.c:%d:%s:\n", line, strerror(errno));
    return 127;

}
