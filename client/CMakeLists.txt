set(CLIENT_SOURCES
    src/main.cpp
    src/MainWindow.cpp
    src/ChatClient.cpp
)

qt_add_executable(KukarachaClient
    MANUAL_FINALIZATION
    ${CLIENT_SOURCES}
)

target_include_directories(KukarachaClient PRIVATE src)

target_link_libraries(KukarachaClient PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network KukarachaCommon)

if (ANDROID)
    set_property(TARGET KukarachaClient PROPERTY QT_ANDROID_PACKAGE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/android")
endif()

qt_finalize_executable(KukarachaClient)

