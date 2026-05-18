cmake_minimum_required(VERSION 3.15)
project(scutclient_windows C)

if(NOT WIN32)
	message(FATAL_ERROR "This fork builds the native Windows version only. Use the original project for Linux/OpenWrt.")
endif()

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)

add_executable(scutclient
	src/auth.c
	src/drcom.c
	src/functions.c
	src/info.c
	src/main.c
	src/md5.c
	src/tracelog.c
)

target_compile_definitions(scutclient PRIVATE
	WIN32_LEAN_AND_MEAN
	_WIN32_WINNT=0x0601
	_CRT_SECURE_NO_WARNINGS
)

if(MSVC)
	target_compile_options(scutclient PRIVATE /W3 /utf-8)
else()
	target_compile_options(scutclient PRIVATE -Wall -Wextra)
endif()

set(NPCAP_SDK "" CACHE PATH "Root directory of the Npcap SDK. It must contain Include/pcap.h and Lib/<arch>/wpcap.lib.")

set(NPCAP_SDK_HINTS
	"${NPCAP_SDK}"
	"$ENV{NPCAP_SDK}"
	"C:/npcap-sdk"
	"C:/npcap-sdk-1.15"
	"C:/npcap-sdk-1.14"
	"C:/Program Files/Npcap SDK"
)

find_path(NPCAP_INCLUDE_DIR
	NAMES pcap.h
	HINTS ${NPCAP_SDK_HINTS}
	PATH_SUFFIXES Include include
)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
	set(NPCAP_LIB_SUFFIXES Lib/x64 Lib/X64 Lib)
else()
	set(NPCAP_LIB_SUFFIXES Lib Lib/x86)
endif()

find_library(NPCAP_WPCAP_LIBRARY
	NAMES wpcap libwpcap
	HINTS ${NPCAP_SDK_HINTS}
	PATH_SUFFIXES ${NPCAP_LIB_SUFFIXES}
)

find_library(NPCAP_PACKET_LIBRARY
	NAMES Packet packet libpacket
	HINTS ${NPCAP_SDK_HINTS}
	PATH_SUFFIXES ${NPCAP_LIB_SUFFIXES}
)

if(NOT NPCAP_INCLUDE_DIR OR NOT NPCAP_WPCAP_LIBRARY OR NOT NPCAP_PACKET_LIBRARY)
	message(FATAL_ERROR
		"Npcap SDK was not found.\n"
		"Install/extract the Npcap SDK, not only the Npcap runtime, and point CMake to the SDK root.\n"
		"The SDK root must contain:\n"
		"  Include\\pcap.h\n"
		"  Lib\\x64\\wpcap.lib and Lib\\x64\\Packet.lib for x64 builds\n"
		"Current hints: ${NPCAP_SDK_HINTS}\n"
		"Examples:\n"
		"  cmake -S . -B build -G \"Visual Studio 17 2022\" -A x64 -DNPCAP_SDK=C:\\npcap-sdk\n"
		"  or in cmd.exe: set NPCAP_SDK=C:\\npcap-sdk")
endif()

target_include_directories(scutclient PRIVATE ${NPCAP_INCLUDE_DIR})
target_link_libraries(scutclient PRIVATE
	ws2_32
	iphlpapi
	${NPCAP_WPCAP_LIBRARY}
	${NPCAP_PACKET_LIBRARY}
)

install(TARGETS scutclient RUNTIME DESTINATION bin)
