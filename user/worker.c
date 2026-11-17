#include "user_api.h"

static char message[] = "Hello from TinyOS worker.\n";

int main(unsigned long argument) {
    (void)argument;
    const long length = (long)(sizeof(message) - 1);
    return user_write(message, (size_t)length) == length ? 0 : 1;
}
