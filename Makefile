#
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mozilla.org code.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1998
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#  Josh Aas <josh@mozilla.com>
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

# Basic plugin requires GTK and X11
#CFLAGS = -Wall -DXP_UNIX=1 -DMOZ_X11=1 -fPIC -g
CFLAGS = -Wall -DXP_UNIX=1 -DMOZ_X11=1 -fPIC -g `pkg-config --cflags --libs glib-2.0`
CC = g++

EMHOST = localhost
EMPORT = 5522

PRE_DEV_DIR=/pre/optware/cs08q1armel/toolchain/arm-2008q1
PRE_DEV_PREFIX=$(PRE_DEV_DIR)/bin/arm-none-linux-gnueabi-
PRE_CC = $(PRE_DEV_PREFIX)g++
PRE_STRIP = $(PRE_DEV_PREFIX)strip
PRE_CFLAGS = -Wall -DXP_UNIX=1 -fPIC -g -I$(PRE_DEV_DIR)/include
PRE_OUT_DIR=pre-out

all : dfbadapter.so

pre:	$(PRE_OUT_DIR)/dfbadapter.so

#dfbadapter : dfbadapter.so

dfbadapter.so: dfbadapter.o
	$(CC) $(CFLAGS) -shared $< -o $@
	@#strip $@

dfbadapter.o : dfbadapter.cpp dfbadapter.h
	$(CC) $(CFLAGS) -c $< -o $@

$(PRE_OUT_DIR)/dfbadapter.so: $(PRE_OUT_DIR)/dfbadapter.o
	$(PRE_CC) $(PRE_CFLAGS) -shared $< -o $@
	@#$(PRE_STRIP) $@
$(PRE_OUT_DIR)/dfbadapter.o: dfbadapter.cpp dfbadapter.h
	mkdir -p $(PRE_OUT_DIR)
	$(PRE_CC) $(PRE_CFLAGS) -c $< -o $@


install : dfbadapter.so
	scp -P $(EMPORT) dfbadapter.so root@$(EMHOST):/usr/lib/BrowserPlugins/; echo done

put:
	novaterm put file:///media/cf/root/dfbadapter/dfbadapter.cpp <dfbadapter.cpp

clean :
	rm -f dfbadapter.so dfbadapter.o $(PRE_OUT_DIR)/dfbadapter.so $(PRE_OUT_DIR)/dfbadapter.o
	( [ -d $(PRE_OUT_DIR) ] && rmdir -p $(PRE_OUT_DIR) || true )
