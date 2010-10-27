
all-local: _mapper.so

%_wrap.c %.py: %.i
	swig -I../include -python mapper.i

_%.so: %_wrap.c
	python setup.py build_ext
	-@mv -v build/lib.linux-i686-2.6/$@ .
	-@rm -rf build

clean-local:
	-@rm -vf _mapper.so mapper.py mapper_wrap.c
