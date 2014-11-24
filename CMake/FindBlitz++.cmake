FIND_PATH(BLITZ_INCLUDE_DIR blitz $ENV{BLITZ_HOME}/include ${INSTALL_DIR}/include)
FIND_LIBRARY(BLITZ_LIBS blitz $ENV{BLITZ_HOME}/lib $ENV{BLITZ_HOME}/lib64 ${INSTALL_DIR}/lib)
IF(BLITZ_INCLUDE_DIR AND BLITZ_LIBS)
    INCLUDE_DIRECTORIES(${BLITZ_INCLUDE_DIR})
    LINK_LIBRARIES(${BLITZ_LIBS})
    MESSAGE(STATUS "BLITZ_INCLUDE_DIR=${BLITZ_INCLUDE_DIR}")
    MESSAGE(STATUS "BLITZ_LIBS=${BLITZ_LIBS}")
ELSE()
    ExternalProject_Add(
      blitz_project
      URL http://downloads.sourceforge.net/project/blitz/blitz/Blitz%2B%2B%200.10/blitz-0.10.tar.gz
      SOURCE_DIR ${DEPENDS_DIR}/blitz
      CONFIGURE_COMMAND ${DEPENDS_DIR}/blitz/configure --enable-optimize --prefix=${INSTALL_DIR}
      BUILD_COMMAND make
    )
    SET(BLITZ_LIBS blitz)
    MESSAGE(STATUS "BLITZ_HOME not found. Installing in DEPENDS_DIR.")
ENDIF()
