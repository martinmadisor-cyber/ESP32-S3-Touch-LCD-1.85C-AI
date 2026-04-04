#include <unity.h>
#include <string>

// A simple function to test (this mimics logic we'd have for station matching)
bool find_match(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return haystack.find(needle) != std::string::npos;
}

void test_match_found() {
    TEST_ASSERT_TRUE(find_match("RMF FM Radio", "RMF"));
}

void test_match_not_found() {
    TEST_ASSERT_FALSE(find_match("BBC Oxford", "RMF"));
}

void test_empty_needle() {
    TEST_ASSERT_FALSE(find_match("Any Radio", ""));
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_match_found);
    RUN_TEST(test_match_not_found);
    RUN_TEST(test_empty_needle);
    UNITY_END();
    return 0;
}
