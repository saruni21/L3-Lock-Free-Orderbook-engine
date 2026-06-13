# src/CMakeLists.txt

set(ORDERBOOK_SOURCES
    orderbook.cpp
    market_data.cpp
    backtester.cpp
)

set(ORDERBOOK_HEADERS
    ${CMAKE_SOURCE_DIR}/include/orderbook/types.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/spsc_queue.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/intrusive_tree.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/arena.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/orderbook.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/market_data.hpp
    ${CMAKE_SOURCE_DIR}/include/orderbook/backtester.hpp
)

# Static library
add_library(orderbook STATIC ${ORDERBOOK_SOURCES} ${ORDERBOOK_HEADERS})
target_include_directories(orderbook PUBLIC 
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(orderbook PUBLIC Threads::Threads)
target_compile_options(orderbook PRIVATE 
    -O3 -march=native -Wall -Wextra -Wpedantic
)

# Main executable
add_executable(ob main.cpp)
target_link_libraries(ob PRIVATE orderbook)
