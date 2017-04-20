#
# @file build.sh
#
# Project Edge
# Copyright (C) 2016-17  Deutsche Telekom Capital Partners Strategic Advisory LLC
#
mkdir -p obj
g++ -g -Wall -std=c++0x -fpic -shared -o obj/latencyresponder.so -I. latencyresponder.cpp
