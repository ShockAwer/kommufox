#
# Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
# Use is subject to license terms.
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
# The Original Code is the Netscape security libraries.
#
# The Initial Developer of the Original Code is
# Netscape Communications Corporation.
# Portions created by the Initial Developer are Copyright (C) 1994-2000
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
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
#
#ident	"$Id: Makefile,v 1.15.2.2 2006/12/06 21:04:21 christophe.ravel.bugs%sun.com Exp $"
#

CORE_DEPTH = ../../..

NAME        = sun-nss
ifndef RPM_RELEASE
RPM_RELEASE = 1
endif
VERSION     = `grep NSS_VERSION $(CORE_DEPTH)/../dist/public/nss/nss.h \
		| head -1 \
		| sed -e 's/[^"]*"//' -e 's/".*//' -e 's/ .*//'`
PWD         = `pwd`
BUILDROOT   = $(PWD)\/$(NAME)-root

include $(CORE_DEPTH)/coreconf/config.mk

# Force i386 for non 64 bit build
ifneq ($(USE_64),1)
	RPMTARGET = "--target=i386"
	RPMLIBDIR = lib
else
	RPMLIBDIR = lib64
endif


publish:
	$(MAKE) clean
	mkdir -p SOURCES SRPMS RPMS BUILD
	mkdir -p opt/sun/private/$(RPMLIBDIR) 
	find $(CORE_DEPTH)/../dist/$(OBJDIR)/lib -type l \
		\( -name "*.so" -o -name "*.chk" \) \
	-exec cp {} opt/sun/private/$(RPMLIBDIR) \;
	rm -f opt/sun/private/$(RPMLIBDIR)/libnspr4.so \
	   opt/sun/private/$(RPMLIBDIR)/libplc4.so \
	   opt/sun/private/$(RPMLIBDIR)/libplds4.so \
	   opt/sun/private/$(RPMLIBDIR)/libjss*.so
	mkdir -p opt/sun/private/bin
	(cd $(CORE_DEPTH)/../dist/$(OBJDIR)/bin && tar cphf - \
		certutil cmsutil crlutil modutil pk12util signtool \
		signver ssltap addbuiltin ) | (cd opt/sun/private/bin && tar xvfBp -)
	(cd $(CORE_DEPTH)/../dist/public && tar cphf - .) \
		| (mkdir -p opt/sun/private/include && cd opt/sun/private/include && tar xvfBp -)
	rm -rf opt/sun/private/include/seccmd
	rm -rf opt/sun/private/include/dbm

	tar czvf $(NAME)-$(VERSION).tar.gz opt 
	echo "%define _topdir `pwd`" >temp.spec
	sed -e "s/NAME_REPLACE/$(NAME)/" \
		-e "s/VERSION_REPLACE/$(VERSION)/" \
		-e "s/RELEASE_REPLACE/$(RPM_RELEASE)/" \
	<$(NAME).spec >>temp.spec
	echo "" >>temp.spec
	echo "%files" >>temp.spec
	echo "%defattr(-,root,root)" >>temp.spec
	echo "%dir /opt" >>temp.spec
	echo "%dir /opt/sun" >>temp.spec
	echo "%dir /opt/sun/private" >>temp.spec
	echo "%dir /opt/sun/private/$(RPMLIBDIR)" >>temp.spec
	echo "%dir /opt/sun/private/bin" >>temp.spec
	find opt \( -name "*.so" -o -name "*.chk" -o -type f \
		-perm u=rwx,g=rx,o=rx \) | sed -e "s-^-/-" >>temp.spec
	echo "" >>temp.spec
	echo "%files devel" >>temp.spec
	echo "%defattr(-,root,root)" >>temp.spec
	echo "%dir /opt" >>temp.spec
	echo "%dir /opt/sun" >>temp.spec
	echo "%dir /opt/sun/private" >>temp.spec
	echo "%dir /opt/sun/private/include" >>temp.spec
	echo "%dir /opt/sun/private/include/nss" >>temp.spec
	find opt -type f \( -name "*.h" \) \
		| sed -e "s-^-/-" >>temp.spec
	cp $(NAME)-$(VERSION).tar.gz SOURCES
	rpmbuild $(RPMTARGET) -bb temp.spec

clean::
	rm -rf SOURCES SRPMS RPMS BUILD
	rm -rf opt 
	rm -f temp.spec
	rm -f $(NAME)-$(VERSION).tar.gz

include $(CORE_DEPTH)/coreconf/rules.mk
