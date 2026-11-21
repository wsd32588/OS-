#include "user_api.h"

static char message[] = "Hello from TinyOS worker.\n";
static char msg[] = "[worker] arg: ";

int main(unsigned long argument, size_t argc, const char* const* argv) {
    (void)argument;
    const long length = (long)(sizeof(message) - 1);
    const long len_msg = (long)(sizeof(msg) - 1);


    if (argc == 2) {
        const char option[] = "--stdin";
        size_t matched = 0;

        while (matched < sizeof(option) && argv[1][matched] == option[matched]) {
            ++matched;
        }

        if (matched == sizeof(option)) {
            unsigned char buffer[16];

            for (;;) {
                const long count = user_read(TINYOS_STDIN_FILENO, buffer, sizeof(buffer));
                if (count == 0) {
                    return 0;
                }
                if (count < 0) {
                    return 1;
                }
                if (user_write_fd(TINYOS_STDOUT_FILENO, buffer, (size_t)count) != count) {
                    return 1;
                }
            }
        }

    }

    if (user_write_fd(TINYOS_STDOUT_FILENO, message, (size_t)length) != length) {
        return 1;
    }

    for (size_t i = 1; i < argc; ++i) {
        const char* argument_text = argv[i];
        size_t argument_length = 0;

        while (argument_text[argument_length] != '\0') {
            ++argument_length;
        }

        if (user_write_fd(TINYOS_STDOUT_FILENO, msg, len_msg) != len_msg) {
            return 1;
        }

        if (user_write_fd(TINYOS_STDOUT_FILENO, argv[i], argument_length) != (int)argument_length) {
            return 1;
        }

        if (user_write_fd(TINYOS_STDOUT_FILENO, "\n", 1) != 1){
            return 1;
        }
    }

    return 0;
}
