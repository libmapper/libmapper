cd $PSScriptRoot

# Replace version env variable with actual version
$versionString = git describe --tags
$versionString = $versionString -replace '([0-9.]+)-([0-9]+)-([a-z0-9]+)', '$1.$2+$3'
(gc ./libmapper/mapper.py) -replace '__version__ = ''@PACKAGE_VERSION@''', "__version__ = '$versionString'" | Out-File -encoding ASCII ./libmapper/mapper.py

# Copy dlls to python bindings folder
cp -v ../../dist/libmapper.dll ./libmapper/libmapper.dll
cp -v ../../dist/liblo.dll ./libmapper/liblo.dll
cp -v ../../dist/zlib.dll ./libmapper/zlib.dll

# Build python wheel
pip install wheel
python setup.py bdist_wheel

Write-Host "Done! Python wheel located in the bindings/python/dist/ folder"