project (ewald CXX C Fortran)

cmake_minimum_required (VERSION 2.6)

#-------------------------------------------------------------------
# Options
#-------------------------------------------------------------------

SET(NDIM 3)
SET(PRECISION double) #double float
SET(BUILD_STATIC FALSE)
SET(COMPILER_MAKE "GNU") # INTEL, GNU, or CLANG
SET(OPTIMIZE TRUE)
SET(DEBUG TRUE)

#-------------------------------------------------------------------
# Compilers
#-------------------------------------------------------------------
IF(${COMPILER_MAKE} MATCHES "INTEL")
  SET(CMAKE_C_COMPILER icc)
  SET(CMAKE_CXX_COMPILER icpc)
  SET(CMAKE_Fortran_COMPILER ifort)
ENDIF()

IF(${COMPILER_MAKE} MATCHES "GNU")
  SET(CMAKE_C_COMPILER gcc)
  SET(CMAKE_CXX_COMPILER g++)
  SET(CMAKE_Fortran_COMPILER gfortran)
ENDIF()

IF(${COMPILER_MAKE} MATCHES "CLANG")
  SET(CMAKE_C_COMPILER clang)
  SET(CMAKE_CXX_COMPILER clang)
  SET(CMAKE_Fortran_COMPILER gfortran)
ENDIF()

#-------------------------------------------------------------------
# Flags
#-------------------------------------------------------------------
SET(COMMON_FLAGS "-w -DNDIM=${NDIM}")

IF(DEBUG)
  SET(COMMON_FLAGS "${COMMON_FLAGS} -pg -Wall")
ELSE(DEBUG)
  SET(COMMON_FLAGS "${COMMON_FLAGS} -DNDEBUG")
ENDIF(DEBUG)

IF(OPTIMIZE)
  SET(COMMON_FLAGS "${COMMON_FLAGS} -O3 -ffast-math -msse3")
ENDIF(OPTIMIZE)

SET(CMAKE_CXX_FLAGS "-std=c++11 ${CMAKE_CXX_FLAGS} ${COMMON_FLAGS}")
SET(CMAKE_CXX_SOURCE_FILE_EXTENSIONS ,"cc")

SET(CMAKE_C_FLAGS "-std=c99 ${CMAKE_C_FLAGS} ${COMMON_FLAGS}")
SET(CMAKE_C_SOURCE_FILE_EXTENSIONS ,"c")

SET(CMAKE_Fortran_FLAGS "${CMAKE_Fortran_FLAGS} ${COMMON_FLAGS}")
SET(CMAKE_Fortran_SOURCE_FILE_EXTENSIONS ,"f")

IF(BUILD_STATIC)
  # Enable static linking
  set(BUILD_STATIC_LIBS TRUE)
  set(LINK_SEARCH_END_STATIC TRUE)
  set(CMAKE_EXE_LINKER_FLAGS "-static -Wl,-Bstatic")
  set(LINK_SEARCH_END_STATIC TRUE)
  set(HDF5_USE_STATIC_LIBRARIES TRUE)

  # Disable shared linking
  set(BUILD_SHARED_LIBS FALSE)
  set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)       # remove -Wl,-Bdynamic
  set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_C_FLAGS)         # remove -fPIC
  set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)
  set(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS)    # remove -rdynamic
  set(CMAKE_SHARED_LIBRARY_LINK_CXX_FLAGS)
ENDIF(BUILD_STATIC)

#-------------------------------------------------------------------
# Directories
#-------------------------------------------------------------------
SET(LIBRARY_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/lib" CACHE PATH "Single output directory for building all libraries")
SET(EXECUTABLE_OUTPUT_PATH "${CMAKE_SOURCE_DIR}/bin" CACHE PATH "Single output directory for building all executables")
SET(CMAKE_INSTALL_PREFIX "$ENV{HOME}" CACHE PATH "Default install to home directory")
SET(PROJECT_CMAKE "${CMAKE_SOURCE_DIR}/CMake")
SET(BUILD_DIR "${CMAKE_SOURCE_DIR}/build")
SET(DEPENDS_DIR "${CMAKE_SOURCE_DIR}/depends")
SET(INSTALL_DIR "${CMAKE_SOURCE_DIR}")

#-------------------------------------------------------------------
# check blitz++
#-------------------------------------------------------------------
INCLUDE(ExternalProject)
INCLUDE_DIRECTORIES(${INSTALL_DIR}/include)
LINK_DIRECTORIES(${INSTALL_DIR}/lib)
INCLUDE(${PROJECT_CMAKE}/FindBlitz++.cmake)

#-------------------------------------------------------------------
# check blas/lapack/atlas/mkl
#-------------------------------------------------------------------
INCLUDE(${PROJECT_CMAKE}/FindLA.cmake)

#-------------------------------------------------------------------
# extra libraries
#-------------------------------------------------------------------
LINK_LIBRARIES("-lz -lpthread -lm -gmp -fopenmp")

#-------------------------------------------------------------------
# add sources
#-------------------------------------------------------------------
SET (SRCS ${SRCS} src/Ewald/ewald.cc
src/Ewald/OptimizedBreakup.cc
src/MatrixOps/MatrixOps.cc
src/Integration/GKIntegration.cc
src/Integration/HermiteQuad.cc
src/Integration/Integrate.cc
src/Splines/BicubicSpline.cc
src/Splines/CubicSpline.cc
src/Splines/Grid.cc
src/Splines/QuinticSpline.cc)
ADD_EXECUTABLE(ewald ${SRCS})
TARGET_LINK_LIBRARIES(ewald ${LIBS} ${LA_LIBS} ${BLITZ_LIBS})
INSTALL(TARGETS ewald DESTINATION $ENV{HOME}/bin)
