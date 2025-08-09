#include <unity.h>

void setUp(void) {
    // set stuff up here
}

void tearDown(void) {
    // clean stuff up here
}



void RUN_UNITY_TESTS() {
    UNITY_BEGIN();
    UNITY_END();
}

#ifdef PLATFORM_ESP32
void app_main() {
    RUN_UNITY_TESTS();
}
#endif

#ifdef PLATFORM_NATIVE
int main(int argc, char **argv) {
    RUN_UNITY_TESTS();
    return 0;
}
#endif