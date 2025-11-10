set(COMMON_SOURCES
    src/ChatMessage.cpp
    src/IMessageSerializer.h
    src/JsonMessageSerializer.cpp
)

add_library(KukarachaCommon STATIC ${COMMON_SOURCES})

target_include_directories(KukarachaCommon PUBLIC src)

target_link_libraries(KukarachaCommon PUBLIC Qt6::Core)

