add_library(sqlite)
target_sources(sqlite
  PRIVATE
    ${sqlite_SOURCE_DIR}/sqlite3.c
  PUBLIC
    ${sqlite_SOURCE_DIR}/sqlite3.h
    ${sqlite_SOURCE_DIR}/sqlite3ext.h
)
