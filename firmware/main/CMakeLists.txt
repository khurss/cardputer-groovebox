idf_component_register(
    SRCS "main.cpp" "hardware.cpp" "sampler_storage.cpp"
    INCLUDE_DIRS "."
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wframe-larger-than=4096 -Werror=frame-larger-than=4096)
