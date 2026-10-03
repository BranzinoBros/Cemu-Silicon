add_library(CemuCommon 
  betype.h
  cpu_features.cpp
  cpu_features.h
  enumFlags.h
  ExceptionHandler/ExceptionHandler.cpp
  ExceptionHandler/ExceptionHandler.h
  FileStream.h
  MemPtr.h
  platform.h
  precompiled.cpp
  precompiled.h
  socket.h
  StackAllocator.h
  SysAllocator.cpp
  SysAllocator.h
  CafeString.h
  version.h
)

target_sources(CemuCommon
PRIVATE
    unix/date.h
    unix/fast_float.h
    unix/platform.cpp
    unix/platform.h
    unix/FileStream_unix.cpp
    unix/FileStream_unix.h
    ExceptionHandler/ExceptionHandler_posix.cpp
)

# CemuCommon always builts the precompiled header
target_precompile_headers(CemuCommon PRIVATE precompiled.h)
target_include_directories(CemuCommon PUBLIC "../")

target_link_libraries(CemuCommon PRIVATE
	glm::glm
)

# PUBLIC because:
# - boost/nowide/convert.hpp is included in precompiled.h
# - boost/predef/os.h is included in platform.h
# - fmt/core.h is included in precompiled.h
target_link_libraries(CemuCommon PUBLIC
	Threads::Threads # necessary because some targets may need -pthreads
	Boost::nowide
	Boost::headers
	fmt::fmt
)
