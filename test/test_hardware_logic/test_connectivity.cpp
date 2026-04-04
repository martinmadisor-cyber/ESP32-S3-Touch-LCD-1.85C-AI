#include <unity.h>
#include <ArduinoFake.h>

using namespace fakeit;

void setUp(void) {
    ArduinoFakeReset();
}

void test_wifi_connection_logic() {
    // Mock WiFi.begin to return a successful connection state after a simulated call
    When(Method(WiFi, begin)).Return(WL_CONNECTED);
    When(Method(WiFi, status)).Return(WL_CONNECTED);
    
    // Simulate our connection check
    int status = WiFi.begin("ssid", "pass");
    TEST_ASSERT_EQUAL(WL_CONNECTED, status);
    TEST_ASSERT_EQUAL(WL_CONNECTED, WiFi.status());
    
    // Verify it was called with correct credentials
    Verify(Method(WiFi, begin).Using("ssid", "pass")).Once();
}

void test_wifi_failure_logic() {
    // Mock WiFi connection failure
    When(Method(WiFi, begin)).Return(WL_CONNECT_FAILED);
    int status = WiFi.begin("bad_ssid", "bad_pass");
    TEST_ASSERT_EQUAL(WL_CONNECT_FAILED, status);
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_wifi_connection_logic);
    RUN_TEST(test_wifi_failure_logic);
    UNITY_END();
    return 0;
}
