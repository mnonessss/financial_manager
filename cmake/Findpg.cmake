include(FindPackageHandleStandardArgs)

# Простейшая реализация: используем PostgreSQL::PostgreSQL
find_package(PostgreSQL REQUIRED)

add_library(pg INTERFACE IMPORTED)

target_include_directories(pg INTERFACE
    ${PostgreSQL_INCLUDE_DIRS}
)

target_link_libraries(pg INTERFACE
    PostgreSQL::PostgreSQL
)

find_package_handle_standard_args(pg
    REQUIRED_VARS PostgreSQL_LIBRARIES PostgreSQL_INCLUDE_DIRS
)


