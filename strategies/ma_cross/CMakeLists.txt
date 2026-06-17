add_library(ma_cross SHARED ma_cross.cpp)
target_link_libraries(ma_cross PRIVATE propr_iface yaml-cpp::yaml-cpp)
set_target_properties(ma_cross PROPERTIES
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  PREFIX ""
)
target_compile_definitions(ma_cross PRIVATE PROPR_PLUGIN_BUILD)

install(FILES params.yaml DESTINATION strategies/ma_cross)
