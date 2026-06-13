# tests/CMakeLists.txt

# Simple test runner (no external dependency for portability)
add_executable(test_orderbook test_orderbook.cpp)
target_link_libraries(test_orderbook PRIVATE orderbook)

add_test(NAME orderbook_unit_tests COMMAND test_orderbook)
