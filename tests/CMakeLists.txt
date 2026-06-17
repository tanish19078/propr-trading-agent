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

# Strategy comparison harness: loads the real strategy plugins and scores them
# across synthetic regimes through the real risk core. Plugin/params paths are
# injected as compile defs; add_dependencies guarantees the .so files exist first.
add_executable(test_strategy_sim integration/test_strategy_sim.cpp
  integration/sim_harness.h ${TEST_FAKES_HEADERS})
target_link_libraries(test_strategy_sim PRIVATE propr_core GTest::gtest GTest::gtest_main)
target_include_directories(test_strategy_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
add_dependencies(test_strategy_sim range_mr momentum donchian ma_cross)
target_compile_definitions(test_strategy_sim PRIVATE
  RANGE_MR_PLUGIN="$<TARGET_FILE:range_mr>"
  RANGE_MR_PARAMS="${CMAKE_SOURCE_DIR}/strategies/range_mr/params.yaml"
  MOMENTUM_PLUGIN="$<TARGET_FILE:momentum>"
  MOMENTUM_PARAMS="${CMAKE_SOURCE_DIR}/strategies/momentum/params.yaml"
  DONCHIAN_PLUGIN="$<TARGET_FILE:donchian>"
  DONCHIAN_PARAMS="${CMAKE_SOURCE_DIR}/strategies/donchian/params.yaml"
  MA_CROSS_PLUGIN="$<TARGET_FILE:ma_cross>"
  MA_CROSS_PARAMS="${CMAKE_SOURCE_DIR}/strategies/ma_cross/params.yaml"
)
gtest_discover_tests(test_strategy_sim)
