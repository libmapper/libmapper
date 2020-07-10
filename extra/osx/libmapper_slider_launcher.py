#!/usr/bin/env python

import os, sys

slider_example = (os.path.abspath(os.path.dirname(sys.argv[0])+"/../../..")
                  + "/libmapper_Slider_Example.app")

os.system("open -n "+slider_example)
