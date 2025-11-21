#include <iostream>
#include <cstring>
#include "LogM.h"
int main() {
    LOG_DEBUG("This is a debug message: x=%d", 42);
    LOG_INFO("This is an info message");
    return 0;
}