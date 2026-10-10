# Банк и кошелёк — отдельной библиотекой: её используют и симуляция, и тесты.
add_library(lab03-bank-core STATIC bank.cpp wallet.cpp)
target_include_directories(lab03-bank-core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(lab03-bank-core PUBLIC Threads::Threads)

add_executable(bank main.cpp simulation.cpp)
target_link_libraries(bank PRIVATE lab03-bank-core)

add_executable(bank-tests tests/bank_tests.cpp)
target_link_libraries(bank-tests PRIVATE lab03-bank-core)
add_test(NAME bank-tests COMMAND bank-tests)
