add_executable(propr_backtest
  main.cpp
  historical_feeder.cpp
  fake_account.cpp
)
target_link_libraries(propr_backtest PRIVATE propr_core)
target_include_directories(propr_backtest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
