set(SERVER_SOURCES
    src/main.cpp
    src/ChatServer.cpp
    src/ClientConnection.cpp
    src/UserStore.cpp
)

add_executable(KukarachaServer ${SERVER_SOURCES})

target_include_directories(KukarachaServer PRIVATE src)

target_link_libraries(KukarachaServer PRIVATE Qt6::Core Qt6::Network KukarachaCommon)

