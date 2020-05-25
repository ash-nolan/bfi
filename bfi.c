#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#define VERSION "0.0"

#define CELL_COUNT 30000
static uint8_t cells[CELL_COUNT] = {0};

static void
errorf(char const* fmt, ...);
static void*
xalloc(void* ptr, size_t size);
static void
xslurp(unsigned char** out_buf, size_t* out_buf_size, char const* path);

static void
usage(void);
static void
argcheck(int argc, char** argv);
static int
run(unsigned char const* source, size_t source_size);

int
main(int argc, char** argv)
{
    unsigned char* source = NULL;
    size_t source_size = 0;

    argcheck(argc, argv);
    xslurp(&source, &source_size, argv[1]);
    int const status = run(source, source_size);

    free(source);
    return status;
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
        errorf("Out of memory!");
        exit(EXIT_FAILURE);
    }
    return ptr;
}

static void
xslurp(unsigned char** out_buf, size_t* out_buf_size, char const* path)
{
    FILE* const stream = fopen(path, "rb");
    if (stream == NULL) {
        errorf("%s!", strerror(errno));
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
        errorf("Failed to slurp file '%s'!", path);
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
        "Usage: bfi FILE"                                          "\n"
        "  -h, --help       Display usage information and exit."   "\n"
        "      --version    Display version information and exit."
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
    }

    if (argc > 2) {
        errorf("More than one file provided!");
        exit(EXIT_FAILURE);
    }
}

static int
run(unsigned char const* source, size_t source_size)
{
    int status = EXIT_SUCCESS;

    size_t* const jumps = xalloc(NULL, source_size * sizeof(size_t));
    size_t* const lines = xalloc(NULL, source_size * sizeof(size_t));
    size_t* const stack = xalloc(NULL, source_size * sizeof(size_t));
    size_t stack_count = 0;
    memset(jumps, 0x00, source_size * sizeof(size_t));
    memset(lines, 0x00, source_size * sizeof(size_t));
    memset(stack, 0x00, source_size * sizeof(size_t));

    //== Construct the bracket jump table.
    size_t line = 1;
    for (size_t i = 0; i < source_size; ++i) {
        lines[i] = line;
        switch (source[i]) {
        case '\n':
            line += 1;
            break;
        case '[':
            stack[stack_count++] = i;
            break;
        case ']':
            if (stack_count == 0) {
                errorf("[line %zu] Unbalanced ']'", line);
                status = EXIT_FAILURE;
                continue;
            }
            stack_count -= 1;
            jumps[stack[stack_count]] = i; // Jump from [ to ]
            jumps[i] = stack[stack_count]; // Jump from ] to [
            break;
        }
    }
    for (size_t i = 0; i < stack_count; ++i) {
        errorf("[line %zu] Unbalanced '['", lines[stack[i]]);
        status = EXIT_FAILURE;
    }
    if (status != EXIT_SUCCESS) {
        goto end;
    }

    //== Execute the source code.
    size_t cell_idx = 0;
    for (size_t pc = 0; pc < source_size; ++pc) {
        int c;
        switch (source[pc]) {
        case '+':
            cells[cell_idx] += 1;
            break;
        case '-':
            cells[cell_idx] -= 1;
            break;
        case '>':
            if (cell_idx == (CELL_COUNT - 1)) {
                errorf("[line %zu] '>' Causes cell out of bounds!", lines[pc]);
                status = EXIT_FAILURE;
                goto end;
            }
            cell_idx += 1;
            break;
        case '<':
            if (cell_idx == 0) {
                errorf("[line %zu] '<' Causes cell out of bounds!", lines[pc]);
                status = EXIT_FAILURE;
                goto end;
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
        }
    }

end:
    free(jumps);
    free(lines);
    free(stack);
    return status;
}
