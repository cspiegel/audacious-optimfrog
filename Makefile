TARGET=	cas-ofr
SRCS=	ofr.cpp
CXX=	g++
OPT=	-O2

OPTIMFROG=	SDK

include compiler.mk
include audacious.mk

LDADD+=		-laudtag -L$(OPTIMFROG)/Library -lOptimFROG -Wl,-rpath,$(OPTIMFROG)/Library
CXXFLAGS+=	-I$(OPTIMFROG)
