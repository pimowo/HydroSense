#ifdef ARDUINO
#include <Arduino.h>
#endif
#include <unity.h>
#include "config.h"

void setUp(void) {}
void tearDown(void) {}

// For Arduino builds the test runner may require these; keep them empty if present
#ifdef ARDUINO
void setup() {}
void loop() {}
#endif

void test_checksum_zero(void) {
    Config c;
    memset(&c, 0, sizeof(Config));
    char cs = calculateChecksum(c);
    TEST_ASSERT_EQUAL_INT8(0, cs);
}

void test_checksum_values(void) {
    Config c;
    memset(&c, 0, sizeof(Config));
    c.tank_full = 123;
    c.tank_empty = 456;
    c.pump_delay = 7;
    // calculate expected manually
    const uint8_t* p = (const uint8_t*)&c;
    char expected = 0;
    for (size_t i = 0; i < offsetof(Config, checksum); i++) expected ^= p[i];
    char cs = calculateChecksum(c);
    TEST_ASSERT_EQUAL_INT8(expected, cs);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_checksum_zero);
    RUN_TEST(test_checksum_values);
    UNITY_END();
    return 0;
}
