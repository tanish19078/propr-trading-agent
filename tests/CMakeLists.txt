include(GoogleTest)

set(TEST_FAKES_HEADERS
  fakes/fake_clock.h
  fakes/fake_http_client.h
)

function(add_propr_test name path)
  add_executable(${name} ${path} ${TEST_FAKES_HEADERS})
  target_link_libraries(${name} PRIVATE propr_core GTest::gtest GTest::gtest_main)
  target_include_directories(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
  gtest_discover_tests(${name})
endfunction()

# Foundation
add_propr_test(test_ulid              unit/test_ulid.cpp)
add_propr_test(test_clock             unit/test_clock.cpp)
add_propr_test(test_decimal           unit/test_decimal.cpp)
add_propr_test(test_rate_limiter      unit/test_rate_limiter.cpp)
add_propr_test(test_sizing_policy     unit/test_sizing_policy.cpp)
add_propr_test(test_account_equity    unit/test_account_equity.cpp)
add_propr_test(test_kill_switch       unit/test_kill_switch.cpp)
add_propr_test(test_journal_replay    unit/test_journal_replay.cpp)

# Architecture
add_propr_test(test_state_machine     unit/test_state_machine.cpp)
add_propr_test(test_preflight         unit/test_preflight.cpp)
add_propr_test(test_schemas_roundtrip unit/test_schemas_roundtrip.cpp)
add_propr_test(test_signed_command    unit/test_signed_command.cpp)
add_propr_test(test_risk_engine       unit/test_risk_engine.cpp)
add_propr_test(test_invariants        unit/test_invariants.cpp)

# Integration / drills
add_executable(test_kill_switch_drill integration/test_kill_switch_drill.cpp ${TEST_FAKES_HEADERS})
target_link_libraries(test_kill_switch_drill PRIVATE propr_core GTest::gtest GTest::gtest_main)
target_include_directories(test_kill_switch_drill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(test_kill_switch_drill)

add_executable(test_simulator_drill integration/test_simulator_drill.cpp ${TEST_FAKES_HEADERS})
target_link_libraries(test_simulator_drill PRIVATE propr_core GTest::gtest GTest::gtest_main)
target_include_directories(test_simulator_drill PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
gtest_discover_tests(test_simulator_drill)
