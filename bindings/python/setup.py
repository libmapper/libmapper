#!/usr/bin/env python

"""
setup.py file for python mapper
"""

from distutils.core import setup
import re
import platform

try:
    from wheel.bdist_wheel import bdist_wheel as _bdist_wheel

    class bdist_wheel(_bdist_wheel):

        def finalize_options(self):
            _bdist_wheel.finalize_options(self)
            # Mark us as not a pure python package
            self.root_is_pure = False

        def get_tag(self):
            python, abi, plat = _bdist_wheel.get_tag(self)
            # We don't contain any python source
            python, abi = 'py3', 'none'
            return python, abi, plat
except ImportError:
    bdist_wheel = None

setup (name         = 'libmapper',
       version      = '2.1',
       author       = "libmapper.org",
       author_email = "dot_mapper@googlegroups.com",
       url          = "http://libmapper.org",
       description  = """A library for mapping controllers and synthesizers.""",
       license      = "GNU LGPL version 2.1 or later",
       packages     = ["libmapper"],
       package_data = {"libmapper": ["libmapper.dll"]},
       cmdclass     = {
        'bdist_wheel': bdist_wheel,
       } if platform.uname()[0] != 'Linux' else {},
)
