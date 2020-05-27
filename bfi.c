#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define VERSION "0.1"

#define CELL_COUNT 30000
static uint8_t cells[CELL_COUNT] = {0};
static size_t cell_idx = 0;

static char const* path = NULL;
static bool debug = false;

static size_t source_size = 0;
static unsigned char* source = NULL;
static size_t* lines = NULL;
static size_t* jumps = NULL;

static void
errorf(char const* fmt, ...);
static void*
xalloc(void* ptr, size_t size);
static void*
xallocz(void* ptr, size_t size);
static void
xslurp(unsigned char** out_buf, size_t* out_buf_size, char const* path);

static void
usage(void);
static void
argcheck(int argc, char** argv);
static bool
prepare(void);
static bool
execute(void);

int
main(int argc, char** argv)
{
    argcheck(argc, argv);
    xslurp(&source, &source_size, path);
    int const status = prepare() && execute();

    free(source);
    free(lines);
    free(jumps);
    return status ? EXIT_SUCCESS : EXIT_FAILURE;
}

static void
errorf(char const* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    fputs("error: ", stderr);
    vfprintf(stderr, fmt, args);
    fputs("\n", stderr);
    va_end(args);
}

static void*
xalloc(void* ptr, size_t size)
{
    if (size == 0) {
        free(ptr);
        return NULL;
    }
    if ((ptr = realloc(ptr, size)) == NULL) {
        errorf("Out of memory");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static void*
xallocz(void* ptr, size_t size)
{
    return memset(xalloc(ptr, size), 0x00, size);
}

static void
xslurp(unsigned char** out_buf, size_t* out_buf_size, char const* path)
{
    FILE* const stream = fopen(path, "rb");
    if (stream == NULL) {
        errorf("%s", strerror(errno));
        exit(EXIT_FAILURE);
    }

    unsigned char* buf = NULL;
    size_t size = 0;

    int c;
    while ((c = fgetc(stream)) != EOF) {
        buf = xalloc(buf, size + 1);
        buf[size++] = (unsigned char)c;
    }
    if (!feof(stream) || ferror(stream)) {
        errorf("Failed to slurp file '%s'", path);
        exit(EXIT_FAILURE);
    }

    fclose(stream);
    *out_buf = buf;
    *out_buf_size = size;
}

static void
usage(void)
{
    // clang-format off
    puts(
        "Usage: bfi FILE"                                            "\n"
        "  -h, --help       Display usage information and exit."     "\n"
        "      --version    Display version information and exit."   "\n"
        "      --debug      Enable the # instruction for debugging."
    );
    // clang-format on
}

static void
argcheck(int argc, char** argv)
{
    if (argc == 1) {
        usage();
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
            usage();
            exit(EXIT_SUCCESS);
        }
        if (strcmp(argv[i], "--version") == 0) {
            puts(VERSION);
            exit(EXIT_SUCCESS);
        }

        if (strcmp(argv[i], "--debug") == 0) {
            debug = true;
            continue;
        }
        if (strncmp(argv[i], "-", 1) == 0 || strncmp(argv[i], "--", 2) == 0) {
            errorf("Unrecognized command line option '%s'", argv[i]);
            exit(EXIT_FAILURE);
        }

        if (path != NULL) {
            errorf("More than one file provided");
            exit(EXIT_FAILURE);
        }
        path = argv[i];
    }
}

//= Prepare and Process the Source Code:
//  (1) Associate line numbers with each byte of the source.
//  (2) Build the jump table for left and right square brackets.
static bool
prepare(void)
{
    bool success = true;

    lines = xallocz(NULL, source_size * sizeof(size_t));
    jumps = xallocz(NULL, source_size * sizeof(size_t));

    size_t* const stack = xallocz(NULL, source_size * sizeof(size_t));
    size_t stack_count = 0;
    size_t line = 1;
    for (size_t i = 0; i < source_size; ++i) {
        lines[i] = line;
        line += source[i] == '\n';
        if (source[i] == '[') {
            stack[stack_count++] = i;
        }
        if (source[i] == ']') {
            if (stack_count == 0) {
                errorf("[line %zu] Unbalanced ']'", line);
                success = false;
                continue;
            }
            stack_count -= 1;
            jumps[stack[stack_count]] = i; // Jump from [ to ]
            jumps[i] = stack[stack_count]; // Jump from ] to [
        }
    }
    for (size_t i = 0; i < stack_count; ++i) {
        errorf("[line %zu] Unbalanced '['", lines[stack[i]]);
        success = false;
    }

    free(stack);
    return success;
}

static bool
execute(void)
{
    int c;
    for (size_t pc = 0; pc < source_size; ++pc) {
        switch (source[pc]) {
        case '+':
            cells[cell_idx] += 1;
            break;
        case '-':
            cells[cell_idx] -= 1;
            break;
        case '>':
            if (cell_idx == (CELL_COUNT - 1)) {
                errorf("[line %zu] '>' causes cell out of bounds", lines[pc]);
                return false;
            }
            cell_idx += 1;
            break;
        case '<':
            if (cell_idx == 0) {
                errorf("[line %zu] '<' causes cell out of bounds", lines[pc]);
                return false;
            }
            cell_idx -= 1;
            break;
        case '[':
            if (cells[cell_idx] == 0) {
                pc = jumps[pc];
            }
            break;
        case ']':
            pc = jumps[pc] - 1;
            break;
        case '.':
            fputc(cells[cell_idx], stdout);
            break;
        case ',':
            if ((c = fgetc(stdin)) != EOF) {
                cells[cell_idx] = (uint8_t)c;
            }
            break;
        case '#':
            if (!debug) {
                continue;
            }
            printf("%5s%-2s%-s\n", "CELL", "", "VALUE (dec|hex)");
            size_t const begin = cell_idx < 2 ? 0 : cell_idx - 2;
            size_t const end = begin + 10;
            for (size_t i = begin; i < end; ++i) {
                unsigned const val = cells[i];
                char const* const endln = i == cell_idx ? " <" : "";
                printf("%05zu%-2s%03u|0x%02X%s\n", i, ":", val, val, endln);
            }
            break;
        }
    }

    return true;
}
