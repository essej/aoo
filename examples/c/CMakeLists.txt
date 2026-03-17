macro(add_example name)
    add_executable(c_${name} "${name}.c" "utils.h" "utils.c")
    target_link_libraries(c_${name} PRIVATE aoo aoo_common PortAudio::portaudio)
    set_target_properties(c_${name} PROPERTIES OUTPUT_NAME ${name})
endmacro()

add_example(simple_sender)
add_example(simple_receiver)
add_example(simple_peer)
