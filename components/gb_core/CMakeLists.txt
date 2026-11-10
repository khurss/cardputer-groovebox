idf_component_register(
    SRCS "src/commands.cpp" "src/drum_ui.cpp" "src/drums.cpp" "src/engine.cpp"
         "src/project_io.cpp" "src/sequencer.cpp" "src/validate.cpp"
    INCLUDE_DIRS "include"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wframe-larger-than=4096 -Werror=frame-larger-than=4096)
