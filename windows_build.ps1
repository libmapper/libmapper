# Create build directory
$scriptDir = Get-Location
if (!(Test-Path "$($scriptDir)\build\")) {
  mkdir build
}
cd build\
# Download zlib and extract zip
if (!(Test-Path "$($scriptDir)\build\zlib\")) {
  Invoke-WebRequest https://www.bruot.org/hp/media/files/libraries/zlib_1_2_11_msvc2017_64.zip -OutFile zlib.zip
  Expand-Archive zlib.zip zlib
  rm zlib.zip
}
# Download liblo and build
if (!(Test-Path "$($scriptDir)\build\liblo\")) {
  Invoke-WebRequest https://github.com/radarsat1/liblo/archive/refs/heads/master.zip -OutFile liblo.zip
  Expand-Archive liblo.zip liblo
  rm liblo.zip
  cd liblo\liblo-master\cmake\
  (gc config-msvc.h.in) -replace '#define HAVE_UNISTD_H', '//#define HAVE_UNISTD_H' | Out-File -encoding ASCII config-msvc.h.in
  mkdir build
  cd build\
  cmake ..
  cmake --build . --target all_build
}
# Build libmapper
cd "$($scriptDir)\build"
cmake ..
cmake --build . --target all_build

# Rename .py.in files to .py for Windows
# Reset active directory
cd $scriptDir
cp ./bindings/python/setup.py.in ./bindings/python/setup.py
cp ./bindings/python/libmapper/mapper.py.in ./bindings/python/libmapper/mapper.py
cd bindings/python/
./get_version.ps1

# Create dist directory for dlls and wheel
cd $scriptDir
if (!(Test-Path "$($scriptDir)\dist\")) {
  mkdir dist
}
# Copy dlls to /dist
cp -v ./build/Debug/libmapper.dll ./dist/libmapper.dll
cp -v ./build/liblo/liblo-master/cmake/build/Debug/liblo.dll ./dist/liblo.dll
cp -v ./build/zlib/msvc2017_64/lib/zlib/zlib.dll ./dist/zlib.dll
# Copy test files
cp -v ./build/test/Debug/* ./dist/tests

Write-Host "Done! dll's for liblo and zlib are located in the dist/ folder"