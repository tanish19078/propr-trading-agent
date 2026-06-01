add_library(range_mr SHARED range_mr.cpp)
target_link_libraries(range_mr PRIVATE propr_iface yaml-cpp::yaml-cpp)
set_target_properties(range_mr PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX ""
)
target_compile_definitions(range_mr PRIVATE PROPR_PLUGIN_BUILD)

install(FILES params.yaml DESTINATION strategies/range_mr)
