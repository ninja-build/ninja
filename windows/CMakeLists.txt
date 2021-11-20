target_sources(ninja PRIVATE 
    ninja.manifest
    ninja.rc.in
)

string(TIMESTAMP NINJA_CURRENT_YEAR "%Y")

configure_file(ninja.rc.in ninja.rc @ONLY)

target_sources(ninja PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}/ninja.rc
)
