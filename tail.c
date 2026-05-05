/*
 * tail.c — clone of the Linux tail(1) utility
 *
 * Supported options:
 *   -n [+]NUM   output the last NUM lines (default 10);
 *               with '+NUM', start from line NUM from the beginning
 *   -c [+]NUM   output the last NUM bytes;
 *               with '+NUM', start from byte NUM from the beginning
 *   -f          keep reading as the file grows (follow mode)
 *   -q          never print file-name headers
 *   -v          always print file-name headers
 *   --          end of options; treat remaining arguments as file names
 *
 * When no file is given (or '-' is given), stdin is read.
 */

#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ------------------------------------------------------------------ */
/* Configuration defaults                                               */
/* ------------------------------------------------------------------ */

#define DEFAULT_LINES   10
#define READ_BUF_SIZE   4096
#define FOLLOW_SLEEP_US 200000   /* 200 ms */

/* ------------------------------------------------------------------ */
/* Option state                                                         */
/* ------------------------------------------------------------------ */

typedef enum { MODE_LINES, MODE_BYTES } TailMode;
typedef enum { FROM_END, FROM_START } TailFrom;

static TailMode   opt_mode      = MODE_LINES;
static TailFrom   opt_from      = FROM_END;
static long long  opt_count     = DEFAULT_LINES;
static int        opt_follow    = 0;
static int        opt_quiet     = 0;   /* -q */
static int        opt_verbose   = 0;   /* -v */

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

static void die(const char *msg)
{
    perror(msg);
    exit(EXIT_FAILURE);
}

static void usage(void)
{
    fputs("Usage: tail [-f] [-q] [-v] [-c [+]NUM | -n [+]NUM] [FILE...]\n"
          "\n"
          "Print the last 10 lines (default) of each FILE to standard output.\n"
          "With no FILE, or when FILE is -, read standard input.\n"
          "\n"
          "  -c [+]NUM   output the last NUM bytes; with '+', skip first NUM-1 bytes\n"
          "  -n [+]NUM   output the last NUM lines; with '+', skip first NUM-1 lines\n"
          "  -f          keep following the file as it grows\n"
          "  -q          never output headers with file names\n"
          "  -v          always output headers with file names\n",
          stderr);
    exit(EXIT_FAILURE);
}

/* Parse the numeric argument for -n / -c.
 * Sets *from to FROM_START when the string starts with '+'. */
static long long parse_count(const char *s, TailFrom *from)
{
    *from = FROM_END;
    if (*s == '+') {
        *from = FROM_START;
        ++s;
    } else if (*s == '-') {
        ++s;  /* treat -n -5 the same as -n 5 */
    }
    if (*s == '\0') {
        fputs("tail: invalid number of lines/bytes\n", stderr);
        exit(EXIT_FAILURE);
    }
    char *end;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || *end != '\0' || v < 0) {
        fprintf(stderr, "tail: invalid number of lines/bytes: '%s'\n", s);
        exit(EXIT_FAILURE);
    }
    return v;
}

/* Print a header line as GNU tail does. */
static void print_header(const char *name)
{
    printf("==> %s <==\n", name);
}

/* ------------------------------------------------------------------ */
/* Core implementations                                                 */
/* ------------------------------------------------------------------ */

/*
 * tail_bytes_from_start: skip the first (count-1) bytes, then copy the rest.
 */
static int tail_bytes_from_start(int fd, long long count)
{
    /* count == 1 means "from byte 1", i.e. skip 0 bytes */
    long long skip = (count > 0) ? count - 1 : 0;
    char buf[READ_BUF_SIZE];

    /* If we can seek, do it efficiently. */
    if (lseek(fd, skip, SEEK_SET) != (off_t)-1) {
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
                die("tail: write error");
        return (n < 0) ? -1 : 0;
    }

    /* Otherwise drain byte-by-byte until we've skipped enough. */
    while (skip > 0) {
        ssize_t chunk = (skip > (long long)sizeof(buf)) ? (ssize_t)sizeof(buf) : (ssize_t)skip;
        ssize_t n = read(fd, buf, (size_t)chunk);
        if (n < 0) return -1;
        if (n == 0) return 0;
        skip -= n;
    }
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0)
        if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
            die("tail: write error");
    return (n < 0) ? -1 : 0;
}

/*
 * tail_bytes_from_end: print the last `count` bytes of fd.
 */
static int tail_bytes_from_end(int fd, long long count)
{
    if (count == 0) return 0;

    /* Try seeking from the end. */
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size != (off_t)-1) {
        off_t offset = (file_size > (off_t)count) ? file_size - (off_t)count : 0;
        if (lseek(fd, offset, SEEK_SET) == (off_t)-1)
            die("tail: lseek");
        char buf[READ_BUF_SIZE];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
                die("tail: write error");
        return (n < 0) ? -1 : 0;
    }

    /* stdin or a pipe: buffer everything in a circular byte buffer. */
    char *ring = malloc((size_t)count);
    if (!ring) die("tail: malloc");

    long long pos = 0;
    long long total = 0;
    char buf[READ_BUF_SIZE];
    ssize_t n;

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            ring[pos % count] = buf[i];
            ++pos;
        }
        total += n;
    }
    if (n < 0) { free(ring); return -1; }

    long long have = (total < count) ? total : count;
    long long start = (total < count) ? 0 : pos % count;

    for (long long i = 0; i < have; ++i) {
        char c = ring[(start + i) % count];
        if (fwrite(&c, 1, 1, stdout) != 1) { free(ring); die("tail: write error"); }
    }
    free(ring);
    return 0;
}

/*
 * tail_lines_from_start: skip first (count-1) newlines, then print the rest.
 * '+1' means "from line 1" — print everything; '+3' skips the first 2 lines.
 */
static int tail_lines_from_start(int fd, long long count)
{
    if (count <= 0) {
        /* print everything */
        char buf[READ_BUF_SIZE];
        ssize_t n;
        while ((n = read(fd, buf, sizeof(buf))) > 0)
            if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
                die("tail: write error");
        return 0;
    }

    long long lines_to_skip = count - 1; /* '+1' means "from line 1", skip 0 */
    char buf[READ_BUF_SIZE];
    ssize_t n;
    int printing = (lines_to_skip == 0);

    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        for (ssize_t i = 0; i < n; ++i) {
            if (printing) {
                if (fwrite(buf + i, 1, 1, stdout) != 1)
                    die("tail: write error");
            } else {
                if (buf[i] == '\n') {
                    --lines_to_skip;
                    if (lines_to_skip == 0)
                        printing = 1;
                }
            }
        }
    }
    return (n < 0) ? -1 : 0;
}

/*
 * tail_lines_from_end: print the last `count` lines of fd.
 *
 * Strategy for seekable files: scan backwards from end counting newlines.
 * Strategy for pipes: circular buffer of lines (store as offsets in a
 * dynamically growing heap buffer).
 */

/* -- seekable version -- */
static int tail_lines_seekable(int fd, long long count)
{
    off_t file_size = lseek(fd, 0, SEEK_END);
    if (file_size == (off_t)-1) return -1;
    if (file_size == 0) return 0;

    /* Read backwards in chunks counting newlines. */
    char buf[READ_BUF_SIZE];
    long long newlines = 0;
    off_t pos = file_size;

    /* If the file ends with a newline we don't count it as an extra line. */
    off_t last_byte_pos = file_size - 1;
    if (lseek(fd, last_byte_pos, SEEK_SET) == (off_t)-1) die("tail: lseek");
    char last;
    if (read(fd, &last, 1) == 1 && last == '\n')
        newlines = -1;  /* compensate */

    while (pos > 0) {
        off_t chunk = (pos > (off_t)sizeof(buf)) ? (off_t)sizeof(buf) : pos;
        pos -= chunk;
        if (lseek(fd, pos, SEEK_SET) == (off_t)-1) die("tail: lseek");
        ssize_t n = read(fd, buf, (size_t)chunk);
        if (n <= 0) break;

        for (ssize_t i = n - 1; i >= 0; --i) {
            if (buf[i] == '\n') {
                ++newlines;
                if (newlines == count) {
                    /* Print from byte after this newline. */
                    off_t start = pos + i + 1;
                    if (lseek(fd, start, SEEK_SET) == (off_t)-1) die("tail: lseek");
                    ssize_t rd;
                    while ((rd = read(fd, buf, sizeof(buf))) > 0)
                        if (fwrite(buf, 1, (size_t)rd, stdout) != (size_t)rd)
                            die("tail: write error");
                    return (rd < 0) ? -1 : 0;
                }
            }
        }
    }

    /* Fewer lines than requested — print everything. */
    if (lseek(fd, 0, SEEK_SET) == (off_t)-1) die("tail: lseek");
    ssize_t rd;
    while ((rd = read(fd, buf, sizeof(buf))) > 0)
        if (fwrite(buf, 1, (size_t)rd, stdout) != (size_t)rd)
            die("tail: write error");
    return (rd < 0) ? -1 : 0;
}

/* -- pipe/stdin version: accumulate in a dynamic buffer -- */
typedef struct {
    char   *data;
    size_t  size;
    size_t  cap;
} ByteBuf;

static void bbuf_push(ByteBuf *b, const char *src, size_t len)
{
    if (b->size + len > b->cap) {
        size_t newcap = (b->cap == 0) ? READ_BUF_SIZE : b->cap * 2;
        while (newcap < b->size + len) newcap *= 2;
        char *p = realloc(b->data, newcap);
        if (!p) die("tail: realloc");
        b->data = p;
        b->cap  = newcap;
    }
    memcpy(b->data + b->size, src, len);
    b->size += len;
}

static int tail_lines_pipe(int fd, long long count)
{
    if (count == 0) return 0;

    ByteBuf buf = {NULL, 0, 0};
    char tmp[READ_BUF_SIZE];
    ssize_t n;

    while ((n = read(fd, tmp, sizeof(tmp))) > 0)
        bbuf_push(&buf, tmp, (size_t)n);
    if (n < 0) { free(buf.data); return -1; }

    if (buf.size == 0) { free(buf.data); return 0; }

    /* Scan backwards counting newlines. */
    long long newlines = 0;
    /* Ignore trailing newline (same as seekable version). */
    size_t end = buf.size;
    if (buf.data[end - 1] == '\n') {
        newlines = -1;
    }

    size_t start_pos = 0;
    for (size_t i = end; i > 0; ) {
        --i;
        if (buf.data[i] == '\n') {
            ++newlines;
            if (newlines == count) {
                start_pos = i + 1;
                break;
            }
        }
    }

    size_t out_len = end - start_pos;
    if (out_len > 0)
        if (fwrite(buf.data + start_pos, 1, out_len, stdout) != out_len)
            die("tail: write error");

    free(buf.data);
    return 0;
}

static int tail_lines_from_end(int fd, long long count)
{
    if (count == 0) return 0;

    /* Attempt seekable path. */
    off_t cur = lseek(fd, 0, SEEK_CUR);
    if (cur != (off_t)-1) {
        return tail_lines_seekable(fd, count);
    }
    return tail_lines_pipe(fd, count);
}

/* ------------------------------------------------------------------ */
/* Follow mode                                                          */
/* ------------------------------------------------------------------ */

static void follow_file(int fd)
{
    char buf[READ_BUF_SIZE];
    struct timespec ts;
    ts.tv_sec  = FOLLOW_SLEEP_US / 1000000;
    ts.tv_nsec = (long)(FOLLOW_SLEEP_US % 1000000) * 1000L;

    for (;;) {
        ssize_t n = read(fd, buf, sizeof(buf));
        if (n > 0) {
            if (fwrite(buf, 1, (size_t)n, stdout) != (size_t)n)
                die("tail: write error");
            fflush(stdout);
        } else if (n == 0) {
            nanosleep(&ts, NULL);
        } else {
            die("tail: read error");
        }
    }
}

/* ------------------------------------------------------------------ */
/* Process a single file descriptor                                     */
/* ------------------------------------------------------------------ */

static int process_fd(int fd)
{
    int rc;

    if (opt_mode == MODE_BYTES) {
        if (opt_from == FROM_START)
            rc = tail_bytes_from_start(fd, opt_count);
        else
            rc = tail_bytes_from_end(fd, opt_count);
    } else {
        if (opt_from == FROM_START)
            rc = tail_lines_from_start(fd, opt_count);
        else
            rc = tail_lines_from_end(fd, opt_count);
    }

    if (rc < 0) return -1;

    if (opt_follow) {
        fflush(stdout);
        follow_file(fd);  /* does not return */
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* main                                                                 */
/* ------------------------------------------------------------------ */

int main(int argc, char *argv[])
{
    int double_dash = 0;
    int argi = 1;

    /* Manual option parsing to support -n +NUM and -c +NUM */
    while (argi < argc && !double_dash) {
        const char *a = argv[argi];
        if (a[0] != '-' || a[1] == '\0') break;  /* not an option or bare '-' */
        if (a[1] == '-' && a[2] == '\0') { double_dash = 1; ++argi; break; }

        /* Walk through combined flags like -fq */
        int i = 1;
        while (a[i] != '\0') {
            char c = a[i];
            switch (c) {
                case 'f': opt_follow  = 1; ++i; break;
                case 'q': opt_quiet   = 1; ++i; break;
                case 'v': opt_verbose = 1; ++i; break;
                case 'n':
                case 'c':
                    opt_mode = (c == 'c') ? MODE_BYTES : MODE_LINES;
                    /* Argument may be attached (e.g. -n5) or the next token. */
                    if (a[i + 1] != '\0') {
                        opt_count = parse_count(a + i + 1, &opt_from);
                        i = (int)strlen(a); /* consume rest */
                    } else {
                        ++argi;
                        if (argi >= argc) { fputs("tail: option requires an argument\n", stderr); return EXIT_FAILURE; }
                        opt_count = parse_count(argv[argi], &opt_from);
                        i = (int)strlen(a); /* exit inner loop */
                    }
                    break;
                default:
                    fprintf(stderr, "tail: invalid option -- '%c'\n", c);
                    usage();
            }
        }
        ++argi;
    }

    /* Remaining argv[argi..] are file names. */
    int n_files = argc - argi;
    int show_headers;

    if (opt_quiet)        show_headers = 0;
    else if (opt_verbose) show_headers = 1;
    else                  show_headers = (n_files > 1);

    if (n_files == 0) {
        /* Read from stdin. */
        if (show_headers) print_header("standard input");
        process_fd(STDIN_FILENO);
        return EXIT_SUCCESS;
    }

    int exit_code = EXIT_SUCCESS;

    for (int fi = argi; fi < argc; ++fi) {
        const char *name = argv[fi];
        int fd;
        int is_stdin = (strcmp(name, "-") == 0);

        if (is_stdin) {
            fd = STDIN_FILENO;
        } else {
            fd = open(name, O_RDONLY);
            if (fd < 0) {
                fprintf(stderr, "tail: cannot open '%s' for reading: %s\n", name, strerror(errno));
                exit_code = EXIT_FAILURE;
                continue;
            }
        }

        if (show_headers) {
            if (fi > argi) putchar('\n');
            print_header(is_stdin ? "standard input" : name);
        }

        if (process_fd(fd) < 0) {
            fprintf(stderr, "tail: error reading '%s': %s\n", name, strerror(errno));
            exit_code = EXIT_FAILURE;
        }

        if (!is_stdin) close(fd);
    }

    fflush(stdout);
    return exit_code;
}
