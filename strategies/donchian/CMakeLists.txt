add_library(donchian SHARED donchian.cpp)
target_link_libraries(donchian PRIVATE propr_iface yaml-cpp::yaml-cpp)
set_target_properties(donchian PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX ""
)
target_compile_definitions(donchian PRIVATE PROPR_PLUGIN_BUILD)

install(FILES params.yaml DESTINATION strategies/donchian)
