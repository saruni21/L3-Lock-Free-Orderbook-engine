# benchmarks/CMakeLists.txt

add_executable(bench_latency bench_latency.cpp)
target_link_libraries(bench_latency PRIVATE orderbook)
