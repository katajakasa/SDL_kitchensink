#!/bin/sh

doxygen Doxyfile
doxyrest docs/xml/index.xml -o docs/tmp/index.rst -F $DOXYREST_FRAME_DIR -f c_index.rst.in
