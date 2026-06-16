find_package(Qt6 REQUIRED COMPONENTS Core Network Nfc WebSockets)

set(SOURCES
    main.cpp
    websdkconnector.cpp
    ravkavloadmanager.cpp
)

set(HEADERS
    websdkconnector.h
    ravkavloadmanager.h
    iso7816apdu.h
)

add_executable(openkav ${SOURCES} ${HEADERS})

target_compile_definitions(openkav PRIVATE APP_VERSION="${PROJECT_VERSION}")

target_link_libraries(openkav
    PRIVATE
        Qt6::Core
        Qt6::Network
        Qt6::Nfc
        Qt6::WebSockets
)