#! /usr/bin/env python

#Written by Kjetil Matheussen: k.s.matheussen@notam02.no


import sys
import urllib2
import readline

sys.path += ["../s7webserver"]

import s7webserver_repl

s7webserver_repl.start("radium>", "http://localhost:5080")

