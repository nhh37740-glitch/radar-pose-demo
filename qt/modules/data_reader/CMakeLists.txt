add_library(radar_data_reader SHARED data_reader.cpp)
target_compile_definitions(radar_data_reader PRIVATE RADAR_READER_BUILD)
target_link_libraries(radar_data_reader PRIVATE radar_contracts Qt6::Core Qt6::Gui)
