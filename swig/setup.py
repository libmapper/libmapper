#!/usr/bin/env python

"""
setup.py file for SWIG mapper
"""

from distutils.core import setup, Extension

top = '..'
mapper_module = Extension('_mapper',
                             sources=['mapper_wrap.c'],
                             include_dirs=[top+'/include'],
                             library_dirs=[top+'/src/.libs',
                                           '/home/sinclairs/.local/lib'],
                             libraries=['mapper-0','lo'],
                             )

setup (name = 'mapper',
       version = '0.1',
       author      = "IDMIL",
       description = """Simple swig mapper""",
       ext_modules = [mapper_module],
       py_modules = ["mapper"],
       )
