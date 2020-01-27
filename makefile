KNOCONFIG       ::= knoconfig
prefix		::= $(shell ${KNOCONFIG} prefix)
libsuffix	::= $(shell ${KNOCONFIG} libsuffix)
KNO_CFLAGS	::= -I. -fPIC $(shell ${KNOCONFIG} cflags)
KNO_LDFLAGS	::= -fPIC $(shell ${KNOCONFIG} ldflags)
PACKAGE_CFLAGS  ::= $(shell etc/pkc --cflags libexif) \
		    $(shell etc/pkc --cflags libqrencode) \
		    $(shell etc/pkc --cflags ImageMagick) \
		    $(shell etc/pkc --cflags MagickWand)
PACKAGE_LDFLAGS ::= $(shell etc/pkc --libs libexif) \
		    $(shell etc/pkc --libs libqrencode) \
		    $(shell etc/pkc --libs ImageMagick) \
		    $(shell etc/pkc --libs MagickWand)
CFLAGS		::= ${CFLAGS} ${PACKAGE_CFLAGS} ${KNO_CFLAGS} 
LDFLAGS		::= ${LDFLAGS} ${PACKAGE_LDFLAGS} ${KNO_LDFLAGS}
CMODULES	::= $(DESTDIR)$(shell ${KNOCONFIG} cmodules)
LIBS		::= $(shell ${KNOCONFIG} libs)
LIB		::= $(shell ${KNOCONFIG} lib)
INCLUDE		::= $(shell ${KNOCONFIG} include)
KNO_VERSION	::= $(shell ${KNOCONFIG} version)
KNO_MAJOR	::= $(shell ${KNOCONFIG} major)
KNO_MINOR	::= $(shell ${KNOCONFIG} minor)
PKG_RELEASE	::= $(cat ./etc/release)
DPKG_NAME	::= $(shell ./etc/dpkgname)
MKSO		::= $(CC) -shared $(CFLAGS) $(LDFLAGS) $(LIBS)
MSG		::= echo
SYSINSTALL      ::= /usr/bin/install -c
MOD_NAME	::= imagetools
MOD_RELEASE     ::= $(shell cat etc/release)
MOD_VERSION	::= ${KNO_MAJOR}.${KNO_MINOR}.${MOD_RELEASE}

GPGID = FE1BC737F9F323D732AA26330620266BE5AFF294
SUDO  = $(shell which sudo)

default build: qrcode.${libsuffix} exif.${libsuffix} imagick.${libsuffix}

%.o: %.c makefile
	@$(CC) $(CFLAGS) -o $@ -c $<
	@$(MSG) CC $@
%.so: %.o
	$(MKSO) $(LDFLAGS) -o $@ $^ ${LDFLAGS}
	@if test ! -z "${COPY_CMODS}"; then cp $@ ${COPY_CMODS}; fi;
	@$(MSG) MKSO  $@ $<
	@ln -sf $(@F) $(@D)/$(@F).${KNO_MAJOR}
%.dylib: %.o makefile
	@$(MACLIBTOOL) -install_name \
		`basename $(@F) .dylib`.${KNO_MAJOR}.dylib \
		${CFLAGS} ${LDFLAGS} -o $@ $(DYLIB_FLAGS) \
		$^
	@if test ! -z "${COPY_CMODS}"; then cp $@ ${COPY_CMODS}; fi;
	@$(MSG) MACLIBTOOL  $@ $<

TAGS: exif.c qrcode.c imagick.c
	etags -o TAGS $<

install: build
	@for mod_name in qrcode exif imagick; do \
	  ${SUDO} ${SYSINSTALL} $${mod_name}.${libsuffix} \
				 ${CMODULES}/$${mod_name}.so.${MOD_VERSION} && \
	  echo === Installed ${CMODULES}/$${mod_name}.so.${MOD_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${MOD_VERSION} \
			${CMODULES}/$${mod_name}.so.${KNO_MAJOR}.${KNO_MINOR} && \
	  echo === Linked ${CMODULES}/${m	od_name}.so.${KNO_MAJOR}.${KNO_MINOR} \
		to $${mod_name}.so.${MOD_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${MOD_VERSION} \
			${CMODULES}/$${mod_name}.so.${KNO_MAJOR} && \
	  echo === Linked ${CMODULES}/$${mod_name}.so.${KNO_MAJOR} \
		to $${mod_name}.so.${MOD_VERSION} && \
	  ${SUDO} ln -sf $${mod_name}.so.${MOD_VERSION} ${CMODULES}/$${mod_name}.so && \
	  echo === Linked ${CMODULES}/$${mod_name}.so to $${mod_name}.so.${MOD_VERSION}; \
	done;

clean:
	rm -f *.o *.${libsuffix}
fresh:
	make clean
	make default

debian: imagick.c qrcode.c exif.c makefile \
	dist/debian/rules dist/debian/control \
	dist/debian/changelog.base
	rm -rf debian
	cp -r dist/debian debian

debian/changelog: debian imagick.c qrcode.c exif.c makefile
	cat debian/changelog.base | etc/gitchangelog kno-imagetools > $@.tmp
	@if test ! -f debian/changelog; then \
	   mv debian/changelog.tmp debian/changelog; \
	 elif diff debian/changelog debian/changelog.tmp 2>&1 > /dev/null; then \
	   mv debian/changelog.tmp debian/changelog; \
	 else rm debian/changelog.tmp; fi

dist/debian.built: imagick.c qrcode.c exif.c makefile debian debian/changelog
	dpkg-buildpackage -sa -us -uc -b -rfakeroot && \
	touch $@

dist/debian.signed: dist/debian.built
	debsign --re-sign -k${GPGID} ../kno-imagetools_*.changes && \
	touch $@

deb debs dpkg dpkgs: dist/debian.signed

dist/debian.updated: dist/debian.signed
	dupload -c ./dist/dupload.conf --nomail --to bionic ../kno-imagetools_*.changes && touch $@

update-apt: dist/debian.updated

debclean: clean
	rm -rf ../kno-imagetools_* ../kno-imagetools-* debian dist/debian.*

debfresh:
	make debclean
	make dist/debian.built
