
all-local: _mapper.so

%_wrap.c %.py: %.i
	swig -I../include -python mapper.i

# Don't interfere with distutils CFLAGS
_%.so: %_wrap.c
	env CFLAGS="" python setup.py build_ext
	-@mv -v build/lib.*/$@ .
	-@rm -rf build

clean-local:
	-@rm -vf _mapper.so mapper.py mapper_wrap.c
