TARGET=	cas-ofr
SRCS=	ofr.cpp
CXX=	g++
OPT=	-O2

PKG=	optimfrog

include compiler.mk
include audacious.mk

LDADD+=		-laudtag
