#ifndef JOYA_HAPTICS_TEST_INDICATOR_H_
#define JOYA_HAPTICS_TEST_INDICATOR_H_

#if defined(CONFIG_JOYA_HAPTICS_TEST_INDICATOR)
#include <stdbool.h>
void haptics_test_indicator_request(bool battery_allows);
#else
#define haptics_test_indicator_request(battery_allows) do { } while (0)
#endif

#endif /* JOYA_HAPTICS_TEST_INDICATOR_H_ */
