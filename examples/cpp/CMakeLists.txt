macro(add_example name)
    add_executable(cpp_${name} "${name}.cpp" "utils.hpp" "utils.cpp")
    target_link_libraries(cpp_${name} PRIVATE aoo aoo_common PortAudio::portaudio)
    set_target_properties(cpp_${name} PROPERTIES OUTPUT_NAME ${name})
endmacro()

add_example(simple_sender)
add_example(simple_receiver)
add_example(simple_peer)
