add_library(momentum SHARED momentum.cpp)
target_link_libraries(momentum PRIVATE propr_iface yaml-cpp::yaml-cpp)
set_target_properties(momentum PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX ""
)
target_compile_definitions(momentum PRIVATE PROPR_PLUGIN_BUILD)

install(FILES params.yaml DESTINATION strategies/momentum)
