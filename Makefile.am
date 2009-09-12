## Makefile.am for DirectFB/systems/pbp

INCLUDES = \
	-I$(top_srcdir)/include		\
	-I$(top_builddir)/include	\
	-I$(top_builddir)/lib		\
	-I$(top_srcdir)/lib		\
	-I$(top_srcdir)/src     

systemsdir = $(MODULEDIR)/systems

systems_LTLIBRARIES = libdirectfb_pbp.la

if BUILD_STATIC
systems_DATA = libdirectfb_pbp.o
endif


libdirectfb_pbp_la_LDFLAGS = \
	-avoid-version	\
	-module		

libdirectfb_pbp_la_SOURCES = \
	pbp.h		\
	pbp.c		\
	primary.c	\
	primary.h	\
	pb_surface_pool.c \
	primary_mem.c \
	primary_mem.h \
	event.h 	


libdirectfb_pbp_la_LIBADD = \
	$(top_builddir)/lib/direct/libdirect.la \
	$(top_builddir)/lib/fusion/libfusion.la \
	$(top_builddir)/src/libdirectfb.la		

#inputdriversdir = $(MODULEDIR)/inputdrivers

#inputdrivers_LTLIBRARIES = libdirectfb_pbinput.la

#if BUILD_STATIC
#inputdrivers_DATA = libdirectfb_pbinput.o
#endif


#libdirectfb_pbinput_la_LDFLAGS = \
#	-framework Carbon	\
#	-avoid-version	\
#	-module

#libdirectfb_pbinput_la_SOURCES = \
#	pbinput.c

#libdirectfb_pbinput_la_LIBADD = \
#	$(top_builddir)/lib/direct/libdirect.la \
#	$(top_builddir)/lib/fusion/libfusion.la \
#	$(top_builddir)/src/libdirectfb.la

bin_PROGRAMS=dfbgdkrun
dfbgdkrun: dfbgdkcontainer.c event.h
	gcc -o dfbgdkrun `pkg-config --cflags --libs gtk+-2.0 gdk-2.0 gthread-2.0` dfbgdkcontainer.c

include $(top_srcdir)/rules/libobject.make	

