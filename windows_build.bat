@echo off
:: Create build directory
if not exist build\ mkdir build
cd build\
:: Download zlib and extract zip
if not exist zlib\ (
  echo Downloading and extracting zlib into build/ folder...
  powershell -Command "Invoke-WebRequest https://www.bruot.org/hp/media/files/libraries/zlib_1_2_11_msvc2017_64.zip -OutFile zlib.zip"
  powershell -Command "Expand-Archive zlib.zip zlib"
  del zlib.zip
)
:: Download liblo and build
if not exist liblo\ (
  echo Downloading and building liblo in build/ folder...
  powershell -Command "Invoke-WebRequest https://github.com/radarsat1/liblo/archive/refs/heads/master.zip -OutFile liblo.zip"
  powershell -Command "Expand-Archive liblo.zip liblo"
  del liblo.zip
  cd liblo\liblo-master\cmake\
  powershell -Command "(gc config-msvc.h.in) -replace '#define HAVE_UNISTD_H', '//#define HAVE_UNISTD_H' | Out-File -encoding ASCII config-msvc.h.in"
  mkdir build
  cd build\
  powershell -Command "cmake .."
  powershell -Command "cmake --build . --target all_build
)
:: Build libmapper
echo "Building libmapper..."
:: Reset active directory
cd /D "%~dp0"
cd build\
powershell -Command "cmake .."
powershell -Command "cmake --build . --target all_build"
:: Build test programs
:: powershell -Command "cp ./Debug/libmapper.dll ../test/"

echo Done! dll's for liblo and zlib are located in the build/ folder
echo build/Debug/ contains the libmapper dll
pause