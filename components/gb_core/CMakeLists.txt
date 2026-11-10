idf_component_register(
    SRCS "src/demo_projects.cpp" "src/sample_player.cpp" "src/wav.cpp" "src/sampler_ui.cpp" "src/arpeggiator.cpp" "src/arrangement_ui.cpp" "src/commands.cpp" "src/drum_ui.cpp" "src/drums.cpp" "src/engine.cpp"
         "src/project_library.cpp" "src/ui_help.cpp" "src/ui_menu.cpp" "src/ui.cpp" "src/instrument_ui.cpp" "src/oscillator.cpp" "src/synth.cpp" "src/recorder.cpp" "src/project_io.cpp" "src/sequencer.cpp" "src/validate.cpp"
    INCLUDE_DIRS "include"
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wframe-larger-than=4096 -Werror=frame-larger-than=4096)
# Inline oscillator math into the voice loop, avoiding per-sample Xtensa FP register spills.
set_source_files_properties(src/sample_player.cpp src/wav.cpp src/synth.cpp src/drums.cpp src/sequencer.cpp src/engine.cpp
                           PROPERTIES COMPILE_OPTIONS "-O3")
