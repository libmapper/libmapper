$versionString = git describe --tags
$versionString = $versionString -replace '([0-9.]+)-([0-9]+)-([a-z0-9]+)', '$1.$2+$3'
(gc ./libmapper/mapper.py) -replace '__version__ = ''@PACKAGE_VERSION@''', "__version__ = '$versionString'" | Out-File -encoding ASCII ./libmapper/mapper.py