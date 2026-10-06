add_library(radar_frontend SHARED frontend.cpp)
target_compile_definitions(radar_frontend PRIVATE RADAR_FRONTEND_BUILD)
target_link_libraries(radar_frontend PUBLIC radar_contracts Qt6::Core Qt6::Gui Qt6::Widgets)
