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
  Invoke-WebRequest https://github.com/radarsat1/liblo/archive/refs/tags/0.35.zip -OutFile liblo.zip
  Expand-Archive liblo.zip liblo
  rm liblo.zip
  cd liblo\liblo-0.35\cmake\
  (gc config-msvc.h.in) -replace '#define HAVE_UNISTD_H', '//#define HAVE_UNISTD_H' | Out-File -encoding ASCII config-msvc.h.in
  mkdir build
  cd build\
  cmake .. -DCMAKE_BUILD_TYPE=Release -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_EXAMPLES=OFF -DWITH_CPP_TESTS=OFF
  cmake --build . --target all_build --config Release
}
# Build libmapper
cd "$($scriptDir)\build"
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target all_build --config Release

# Create dist directory for dlls and wheel
cd $scriptDir
if (!(Test-Path "$($scriptDir)\dist\")) {
  mkdir dist
}
# Copy dlls to /dist
cp -v ./build/Release/libmapper.dll ./dist/libmapper.dll
cp -v ./build/liblo/liblo-0.35/cmake/build/Release/liblo.dll ./dist/liblo.dll
cp -v ./build/zlib/msvc2017_64/lib/zlib/zlib.dll ./dist/zlib.dll
# Copy test files
cp -v ./build/test/Release/* ./dist/tests

Write-Host "Done! dll's for liblo and zlib are located in the dist/ folder"
