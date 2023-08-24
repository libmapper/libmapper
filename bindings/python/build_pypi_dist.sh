echo preparing distribution for PyPI...

mkdir pypi
mkdir pypi/src
mkdir pypi/src/libmapper
cp libmapper/__init__.py pypi/src/libmapper/
cp libmapper/mapper.py pypi/src/libmapper/

mkdir pypi/tests
cp test*.py pypi/tests/
cp tkgui.py pypi/tests/

cp pyproject.toml pypi/

cp setup.py pypi/
sed -i '' 's#\[\"libmapper\"\]#\[\"src/libmapper\"\]#' pypi/setup.py

cp ../../COPYING pypi/LICENSE
cp README.md pypi/

cd pypi
python3 -m build

rm dist/libmapper-*-py3-none-any.whl
cp ../artifact/libmapper-*.whl dist/
cp ../artifact/wheelhouse/libmapper-*.whl dist/

echo done!
