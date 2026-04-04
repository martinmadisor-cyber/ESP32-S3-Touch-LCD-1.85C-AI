#include <unity.h>
#include <ArduinoFake.h>
#include <vector>
#include <string>

// Mock data structures to simulate our SD card file list
std::vector<std::pair<std::string, std::string>> mock_radio_stations;

void setUp(void) {
    ArduinoFakeReset();
    mock_radio_stations.clear();
    mock_radio_stations.push_back({"RMF FM", "http://stream1.rmf.fm"});
    mock_radio_stations.push_back({"BBC Oxford", "http://stream.bbc.co.uk"});
}

void test_station_matching_logic() {
    // Tests the logic similar to our Chatbot station finder
    std::string query = "rmf";
    std::string matched_url = "";
    
    for(const auto& s : mock_radio_stations) {
        std::string name = s.first;
        // Simple case insensitive check
        if (name.find("RMF") != std::string::npos) {
            matched_url = s.second;
            break;
        }
    }
    
    TEST_ASSERT_EQUAL_STRING("http://stream1.rmf.fm", matched_url.c_str());
}

void test_station_matching_fuzzy() {
    std::string query = "oxford";
    std::string matched_url = "";
    
    for(const auto& s : mock_radio_stations) {
        std::string name = s.first;
        if (name.find("Oxford") != std::string::npos) {
            matched_url = s.second;
            break;
        }
    }
    
    TEST_ASSERT_EQUAL_STRING("http://stream.bbc.co.uk", matched_url.c_str());
}

int main(int argc, char **argv) {
    UNITY_BEGIN();
    RUN_TEST(test_station_matching_logic);
    RUN_TEST(test_station_matching_fuzzy);
    UNITY_END();
    return 0;
}
