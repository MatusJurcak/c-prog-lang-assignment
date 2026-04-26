#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK_SIZE 512
#define NUMBER_OF_EMPTY_BLOCKS_TO_END 2
#define SIZE_FIELD_LENGTH 12

#define OLDGNU_MAGIC "ustar  "
#define TMAGIC "ustar"
#define PROGRAM_NAME "mytar"
#define REQUIRED_ARG_CHAR ':'
#define FLAG_CHAR '-'
#define REGULAR_FILE_TYPE '0'
#define AREGULAR_FILE_TYPE '\0'

#define AT_LEAST_ONE_OPTION_MESSAGE "need at least one option\n"
#define NON_RECOVERABLE_MESSAGE "Error is not recoverable: exiting now\n"
#define UNEXPECTED_EOF_IN_ARCHIVE_MESSAGE "Unexpected EOF in archive\n"
#define WITH_PREVIOUS_ERRORS_MESSAGE "Exiting with failure status due to previous errors\n"
#define BOTH_T_AND_X_OPTIONS_SPECIFIED_ERROR_MESSAGE "Can't specify both -x and -t\n"
#define DOES_NOT_LOOK_LIKE_TAR_ERROR_MESSAGE "This does not look like a tar archive\n"
#define NO_INPUT_FILE_SPECIFIED_ERROR_MESSAGE "No input file specified\n"
#define ERROR_CLOSING_FILE_MESSAGE "Error closing file\n"

#define PROGRAM_ERROR_MESSAGE_PREFIX_FORMAT "%s: "
#define REQUIRED_ARG_ERR_FORMAT "Option -%c requires an argument\n"
#define UNKNOWN_OPTION_ERR_FORMAT "Unknown option: -%c\n"
#define UNSUPPORTED_HEADER_TYPE_ERROR_FORMAT "Unsupported header type: %d\n"
#define OPENING_FILE_ERROR_FORMAT "Error opening archive: Failed to open '%s'\n"
#define LONE_BLOCK_ERROR_FORMAT "A lone zero block at %d\n"
#define NOT_FOUND_IN_ARCHIVE_ERROR_FORMAT "%s: Not found in archive\n"

// Global variables for options that simulate getopt behaviour
int optindex = 1;
char * optargument = NULL;

struct {
    int t;
    // TODO: verbose levels?
    int v;
    int x;
    FILE * f;
} options;

// https://man7.org/linux/man-pages/man1/tar.1.html#RETURN_VALUE
enum ret_val {
    SUCCESS,
    SOME_FILES_DIFFER,
    FATAL_ERROR
};

typedef enum error_type {
    BASIC,
    WITH_PREV_ERRORS,
    NON_RECOVERABLE
} error_type_t;

// https://pubs.opengroup.org/onlinepubs/9699919799/utilities/pax.html#tag_20_92_13_06
typedef struct ustar_header_block {
    char name[100];
    char mode[8];
    char uid[8];
    char gid[8];
    char size[SIZE_FIELD_LENGTH];
    char mtime[12];
    char chksum[8];
    char typeflag;
    char linkname[100];
    char magic[6];
    char version[2];
    char uname[32];
    char gname[32];
    char devmajor[8];
    char devminor[8];
    char prefix[155];
} ustar_header_block_t;

typedef struct tar_files {
    size_t size;
    char const ** file_names;
    bool * present_in_tar;
} tar_files_t;

void
print_error_message_with_prefix(char const * error_message_format, va_list error_message_args) {
    fprintf(stderr, PROGRAM_ERROR_MESSAGE_PREFIX_FORMAT, PROGRAM_NAME);
    vfprintf(stderr, error_message_format, error_message_args);
}

void
print_error(char const * error_message_format, ...) {
    va_list args;
    va_start(args, error_message_format);
    print_error_message_with_prefix(error_message_format, args);
    va_end(args);
}

void
check_for_t_and_x_options() {
    if (options.t == 1 && options.x == 1) {
        print_error(BOTH_T_AND_X_OPTIONS_SPECIFIED_ERROR_MESSAGE);
        exit(FATAL_ERROR);
    }
}

void
check_for_no_input_file_specified() {
    if (options.f == NULL) {
        // I don't really know if this is the right way to handle this, because in this situation
        // on my machine the program is in an infinite loop so I'd rather just terminate the program
        print_error(NO_INPUT_FILE_SPECIFIED_ERROR_MESSAGE);
        exit(FATAL_ERROR);
    }
}

void
check_tar_archive_validity() {
    ustar_header_block_t header;

    if (fread(&header, sizeof(header), 1, options.f) != 1) {
        print_error(UNEXPECTED_EOF_IN_ARCHIVE_MESSAGE);
        print_error(NON_RECOVERABLE_MESSAGE);
        exit(FATAL_ERROR);
    };

    if (strcmp(header.magic, TMAGIC) && strcmp(header.magic, OLDGNU_MAGIC)) {
        print_error(DOES_NOT_LOOK_LIKE_TAR_ERROR_MESSAGE);
        print_error(WITH_PREVIOUS_ERRORS_MESSAGE);
        exit(FATAL_ERROR);
    }

    rewind(options.f);
}

void
check_for_main_errors() {
    check_for_t_and_x_options();

    check_for_no_input_file_specified();

    check_tar_archive_validity();
}

int
getoption(int argc, char * argv[], char const * optstring) {
    int pos = 1;

    optargument = NULL;

    if (optindex >= argc) {
        return -1;
    }

    char * arg = argv[optindex];

    // Not an option
    if (arg[0] != FLAG_CHAR) {
        return -1;
    }

    char c = arg[pos];
    char const * p = strchr(optstring, c);

    if (p == NULL) {
        print_error(UNKNOWN_OPTION_ERR_FORMAT, c);
        exit(FATAL_ERROR);
    }

    // Option that takes a required argument
    if (p[1] == REQUIRED_ARG_CHAR) {
        if (optindex + 1 < argc) {
            optindex++;
            optargument = argv[optindex];
        } else {
            print_error(REQUIRED_ARG_ERR_FORMAT, c);
            exit(FATAL_ERROR);
        }
    }

    optindex++;
    return c;
}

bool
check_if_block_is_empty(char const * block) {
    for (size_t i = 0; i < BLOCK_SIZE; i++) {
        if (block[i] != 0) {
            return false;
        }
    }

    return true;
}

bool
check_if_file_is_present_in_tar(tar_files_t const * tar_files, ustar_header_block_t const * header_block) {
    for (size_t i = 0; i < tar_files->size; i++) {
        if (strcmp(tar_files->file_names[i], header_block->name) == 0) {
            tar_files->present_in_tar[i] = true;
            return true;
        }
    }

    return false;
}

void
print_file_name(ustar_header_block_t const * header_block) {
    printf("%s\n", header_block->name);
}

void
print_file_name_if_should_be_printed(tar_files_t const * tar_files, ustar_header_block_t const * header_block) {
    // If no files were specified, we print the name of every file in the archive
    // Otherwise, only the files present in archive that were specified should be printed
    if (tar_files->size == 0 || check_if_file_is_present_in_tar(tar_files, header_block)) {
        print_file_name(header_block);
    }
}

size_t
get_file_size_from_header(ustar_header_block_t const * header_block) {
    unsigned char const * size = (unsigned char const *) header_block->size;

    // Base-256 encoding (star extension)
    if (size[0] & 0x80) {
        size_t result = (size[0] & 0x7F);

        for (size_t i = 1; i < SIZE_FIELD_LENGTH; i++) {
            result <<= 8;
            result |= size[i];
        }

        return result;
    }

    // Standard octal encoding
    return strtoull((char const *) size, NULL, 8);
}

size_t
get_file_size_from_file(FILE * f) {
    // Save the current file position to restore it later
    long current_file_position = ftell(f);
    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);

    // Restore the file position to what it was before
    fseek(f, current_file_position, SEEK_SET);

    return (size_t) file_size;
}

void
jump_to_next_file_in_archive(ustar_header_block_t const * header_block, size_t archive_size) {
    size_t file_size_from_header = get_file_size_from_header(header_block);
    size_t num_blocks = (file_size_from_header + BLOCK_SIZE - 1) / BLOCK_SIZE;
    size_t offset = num_blocks * BLOCK_SIZE;

    if (ftell(options.f) + offset > archive_size) {
        print_error(UNEXPECTED_EOF_IN_ARCHIVE_MESSAGE);
        print_error(NON_RECOVERABLE_MESSAGE);
        exit(FATAL_ERROR);
    }

    fseek(options.f, offset, SEEK_CUR);
}

void
read_file_and_write(char * block, size_t size_to_read, FILE * output_file) {
    size_t const ret_code = fread(block, 1, size_to_read, options.f);

    if (ret_code == size_to_read) {
        if (fwrite(block, 1, size_to_read, output_file) != size_to_read) {
            print_error(NON_RECOVERABLE_MESSAGE);
            exit(FATAL_ERROR);
        }
    } else {
        // Error handling
        if (feof(options.f)) {
            print_error(UNEXPECTED_EOF_IN_ARCHIVE_MESSAGE);
            print_error(NON_RECOVERABLE_MESSAGE);
            exit(FATAL_ERROR);
        } else if (ferror(options.f)) {
            print_error(NON_RECOVERABLE_MESSAGE);
            exit(FATAL_ERROR);
        }
    }
}

void
extract_file(tar_files_t const * tar_files, ustar_header_block_t const * header_block, size_t archive_size) {
    if (tar_files->size != 0 && !check_if_file_is_present_in_tar(tar_files, header_block)) {
        jump_to_next_file_in_archive(header_block, archive_size);
        return;
    }

    FILE * output_file = fopen(header_block->name, "w");

    if (output_file == NULL) {
        print_error(OPENING_FILE_ERROR_FORMAT, header_block->name);
        exit(FATAL_ERROR);
    }

    char block[BLOCK_SIZE];
    size_t file_size = get_file_size_from_header(header_block);

    size_t full_blocks = file_size / BLOCK_SIZE;
    size_t remainder = file_size % BLOCK_SIZE;

    for (size_t i = 0; i < full_blocks; i++) {
        read_file_and_write(block, BLOCK_SIZE, output_file);
    }

    if (remainder > 0) {
        read_file_and_write(block, remainder, output_file);
        fseek(options.f, BLOCK_SIZE - remainder, SEEK_CUR);
    }


    if (fclose(output_file) == EOF) {
        print_error(ERROR_CLOSING_FILE_MESSAGE);
        exit(FATAL_ERROR);
    }
}

void
execute_main_tar_processing_logic(int argc, char * argv[]) {
    size_t archive_size = get_file_size_from_file(options.f);
    rewind(options.f);


    // I'm assuming that all the specified files need to be after all the options
    size_t number_of_files = argc - optindex;

    bool present_in_tar[number_of_files];

    // Initialize the array to false
    memset(&present_in_tar, 0, sizeof(present_in_tar));

    tar_files_t tar_files = {
        .size = number_of_files,
        .file_names = (char const **) (argv + optindex),
        .present_in_tar = present_in_tar,
    };


    char block[BLOCK_SIZE];
    uint8_t num_of_empty_blocks = 0;
    ustar_header_block_t file_header_block;

    while (true) {
        size_t const ret_code = fread(block, 1, BLOCK_SIZE, options.f);
        if (ret_code == 0) {
            break;
        } else if (ret_code == BLOCK_SIZE) {
            if (check_if_block_is_empty(block)) {
                num_of_empty_blocks++;
                // Archive ends with two empty blocks
                if (num_of_empty_blocks == NUMBER_OF_EMPTY_BLOCKS_TO_END) {
                    break;
                }
                continue;
            } else {
                num_of_empty_blocks = 0;
            }

            file_header_block = *(ustar_header_block_t *) block;

            if (file_header_block.typeflag != REGULAR_FILE_TYPE && file_header_block.typeflag != AREGULAR_FILE_TYPE) {
                print_error(UNSUPPORTED_HEADER_TYPE_ERROR_FORMAT, file_header_block.typeflag);
                exit(FATAL_ERROR);
            }

            // At this point the following is true:
            // 1. options.f is not NULL
            // 2. options.t and options.x are not both 1

            // So we handle 3 possible cases:
            // 1. options.t == 1: (we do not care about option.v in this case)
            // 2. options.x == 1 and options.v == 1
            // 3. options.x == 1 and options.v == 0

            if (options.t) {
                print_file_name_if_should_be_printed(&tar_files, &file_header_block);
                jump_to_next_file_in_archive(&file_header_block, archive_size);
            } else if (options.x) {
                if (options.v) {
                    print_file_name_if_should_be_printed(&tar_files, &file_header_block);
                }
                extract_file(&tar_files, &file_header_block, archive_size);
            }

        } else {
            if (feof(options.f)) {
                print_error(UNEXPECTED_EOF_IN_ARCHIVE_MESSAGE);
                print_error(NON_RECOVERABLE_MESSAGE);
                exit(FATAL_ERROR);
            } else if (ferror(options.f)) {
                print_error(NON_RECOVERABLE_MESSAGE);
                exit(FATAL_ERROR);
            }
        }
    }

    if (num_of_empty_blocks == 1) {
        // Warning for 1 missing block, missing both is silently accepted
        print_error(LONE_BLOCK_ERROR_FORMAT, (int) ftell(options.f) / BLOCK_SIZE);
    }

    bool all_files_present = true;
    for (size_t i = 0; i < tar_files.size; i++) {
        if (!tar_files.present_in_tar[i]) {
            all_files_present = false;
            print_error(NOT_FOUND_IN_ARCHIVE_ERROR_FORMAT, tar_files.file_names[i]);
        }
    }

    if (!all_files_present) {
        print_error(WITH_PREVIOUS_ERRORS_MESSAGE);
        exit(FATAL_ERROR);
    }

    if (fclose(options.f) == EOF) {
        print_error(ERROR_CLOSING_FILE_MESSAGE);
        exit(FATAL_ERROR);
    }
}

int
main(int argc, char * argv[]) {
    // Need at least 1 argument other than the file name
    if (argc == 1) {
        print_error(AT_LEAST_ONE_OPTION_MESSAGE);
        return FATAL_ERROR;
    }

    int opt;

    while ((opt = getoption(argc, argv, "f:tvx")) != -1) {
        switch (opt) {
        case 't':
            options.t = 1;
            break;
        case 'x':
            options.x = 1;
            break;
        case 'v':
            options.v = 1;
            break;
        case 'f':
            options.f = fopen(optargument, "r");
            if (options.f == NULL) {
                print_error(OPENING_FILE_ERROR_FORMAT, optargument);
                return FATAL_ERROR;
            }
            break;
        }
    }

    check_for_main_errors();

    execute_main_tar_processing_logic(argc, argv);

    return SUCCESS;
}
