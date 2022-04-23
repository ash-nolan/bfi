#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define CELL_COUNT 30000
static uint8_t cells[CELL_COUNT] = {0};
static size_t cellptr = 0;

static char const* path = NULL;

static size_t source_size = 0;
static unsigned char* source = NULL;
static size_t* lines = NULL; // <- Line numbers for each source byte.
static size_t* jumps = NULL; // <- Jump indices for each [ and ] instruction.

static void
errorf(char const* fmt, ...);
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
    bool const status = prepare() && execute();

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
        buf = realloc(buf, size + 1);
        if (buf == NULL) {
            errorf("Out of memory");
            exit(EXIT_FAILURE);
        }
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
    puts("Usage: bfi FILE");
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

// Iterate over the source buffer and do the following:
//  (1) Setup lines by associating line numbers with each source byte.
//  (2) Setup jumps by pairing [ and ] instructions.
//  (3) Detect unbalanced [ and ] instructions (invalid source code).
static bool
prepare(void)
{
    bool success = true;

    assert(source != NULL);
    lines = calloc(source_size, sizeof(size_t));
    jumps = calloc(source_size, sizeof(size_t));
    size_t* const stack = calloc(source_size, sizeof(size_t));
    size_t stack_count = 0;

    if (lines == NULL || jumps == NULL || stack == NULL) {
        errorf("Out of memory");
        exit(EXIT_FAILURE);
    }

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
    assert(source != NULL);
    assert(lines != NULL);
    assert(jumps != NULL);

    int c;
    for (size_t pc = 0; pc < source_size; ++pc) {
        switch (source[pc]) {
        case '+':
            cells[cellptr] += 1;
            break;
        case '-':
            cells[cellptr] -= 1;
            break;
        case '>':
            if (cellptr == (CELL_COUNT - 1)) {
                errorf("[line %zu] '>' causes cell out of bounds", lines[pc]);
                return false;
            }
            cellptr += 1;
            break;
        case '<':
            if (cellptr == 0) {
                errorf("[line %zu] '<' causes cell out of bounds", lines[pc]);
                return false;
            }
            cellptr -= 1;
            break;
        case '[':
            if (cells[cellptr] == 0) {
                pc = jumps[pc];
            }
            break;
        case ']':
            pc = jumps[pc] - 1;
            break;
        case '.':
            fputc(cells[cellptr], stdout);
            break;
        case ',':
            if ((c = fgetc(stdin)) != EOF) {
                cells[cellptr] = (uint8_t)c;
            }
            break;
        }
    }

    return true;
}
