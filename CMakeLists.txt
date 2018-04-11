project(focal)
cmake_minimum_required(VERSION 2.8)

add_definitions(-D_GNU_SOURCE)
set(CMAKE_C_FLAGS_DEBUG "-g -Wall -Werror -Wno-pointer-sign")

find_package(PkgConfig REQUIRED)

# GTK3
pkg_check_modules(GTK3 REQUIRED gtk+-3.0)
include_directories(${GTK3_INCLUDE_DIRS})
link_directories(${GTK3_LIBRARY_DIRS})
add_definitions(${GTK3_CFLAGS_OTHER})

# LibXML2
find_package(LibXml2 REQUIRED)
include_directories(${LIBXML2_INCLUDE_DIR})
add_definitions(${LIBXML2_DEFINITIONS})

# cURL
find_package(CURL REQUIRED)
include_directories(${CURL_INCLUDE_DIRS})
add_definitions(${LIBXML2_DEFINITIONS})

# libical
pkg_check_modules(ICAL REQUIRED libical)
include_directories(${ICAL_INCLUDE_DIRS})

# executable
add_executable(${PROJECT_NAME}
	caldav-client.c
	calendar.c
	event-panel.c
	main.c
	remote-calendar.c
	rpc.c
	week-view.c
)

target_link_libraries(${PROJECT_NAME}
	${GTK3_LIBRARIES}
	${LIBXML2_LIBRARIES}
	${CURL_LIBRARIES}
	${ICAL_LIBRARIES}
)
