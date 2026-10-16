cmake_minimum_required(VERSION 3.10)
project(Hermes_MQTT_App C)

set(CMAKE_C_STANDARD 99)

# 1. Setup Paho MQTT
set(PAHO_DIR ${CMAKE_CURRENT_SOURCE_DIR}/paho.mqtt.c)
include_directories(${PAHO_DIR}/src)
link_directories(${PAHO_DIR}/build/output)

# 2. Setup Protobuf-C
find_program(PROTOC_C protoc-c REQUIRED)
find_library(PROTOBUF_C_LIB protobuf-c REQUIRED)

# Generate C code from .proto file
set(PROTO_FILE ${CMAKE_CURRENT_SOURCE_DIR}/schema.proto)
set(PROTO_SRC ${CMAKE_CURRENT_BINARY_DIR}/schema.pb-c.c)
set(PROTO_HDR ${CMAKE_CURRENT_BINARY_DIR}/schema.pb-c.h)

add_custom_command(
    OUTPUT ${PROTO_SRC} ${PROTO_HDR}
    COMMAND ${PROTOC_C} --c_out=${CMAKE_CURRENT_BINARY_DIR} -I${CMAKE_CURRENT_SOURCE_DIR} ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

include_directories(${CMAKE_CURRENT_BINARY_DIR})

# 3. Create our executable
add_executable(hermes_app main.c ${PROTO_SRC})

# 4. Link our executable against the paho-mqtt3c library and protobuf-c
target_link_libraries(hermes_app paho-mqtt3c ${PROTOBUF_C_LIB})

# 5. Run target
# Pass credentials at cmake configure time — they are never stored in source code.
# Usage:  cmake .. -DMQTT_USER=hermes -DMQTT_PASS=yourpassword -DINTERVAL=5
set(MQTT_USER "hermes"   CACHE STRING "MQTT broker username")
set(MQTT_PASS "changeme" CACHE STRING "MQTT broker password")
set(INTERVAL  "3"        CACHE STRING "Telemetry publish interval in seconds")

add_custom_target(run
    COMMAND env LD_LIBRARY_PATH=${PAHO_DIR}/build/output
        $<TARGET_FILE:hermes_app>
        --user ${MQTT_USER}
        --pass ${MQTT_PASS}
        --interval ${INTERVAL}
    DEPENDS hermes_app
)
