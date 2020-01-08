prefix		::= $(shell knoconfig prefix)
libsuffix	::= $(shell knoconfig libsuffix)
KNO_CFLAGS	::= -I. -fPIC $(shell knoconfig cflags)
KNO_LDFLAGS	::= -fPIC $(shell knoconfig ldflags)
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
CMODULES	::= $(DESTDIR)$(shell knoconfig cmodules)
LIBS		::= $(shell knoconfig libs)
LIB		::= $(shell knoconfig lib)
INCLUDE		::= $(shell knoconfig include)
KNO_VERSION	::= $(shell knoconfig version)
KNO_MAJOR	::= $(shell knoconfig major)
KNO_MINOR	::= $(shell knoconfig minor)
PKG_RELEASE	::= $(cat ./etc/release)
DPKG_NAME	::= $(shell ./etc/dpkgname)
MKSO		::= $(CC) -shared $(CFLAGS) $(LDFLAGS) $(LIBS)
MSG		::= echo
SYSINSTALL      ::= /usr/bin/install -c
MOD_NAME	::= imagetools
MOD_RELEASE     ::= $(shell cat etc/release)
MOD_VERSION	::= ${KNO_MAJOR}.${KNO_MINOR}.${MOD_RELEASE}

GPGID           ::= FE1BC737F9F323D732AA26330620266BE5AFF294
SUDO            ::= $(shell which sudo)

default: qrcode.so exif.so imagick.so

%.o: %.c makefile
	@$(CC) $(CFLAGS) -o $@ -c $<
	@$(MSG) CC $@
%.so: %.o
	$(MKSO) $(LDFLAGS) -o $@ $^ ${LDFLAGS}
	@$(MSG) MKSO  $@ $<
	@ln -sf $(@F) $(@D)/$(@F).${KNO_MAJOR}
%.dylib: %.o makefile
	@$(MACLIBTOOL) -install_name \
		`basename $(@F) .dylib`.${KNO_MAJOR}.dylib \
		${CFLAGS} ${LDFLAGS} -o $@ $(DYLIB_FLAGS) \
		$^
	@$(MSG) MACLIBTOOL  $@ $<

TAGS: exif.c qrcode.c imagick.c
	etags -o TAGS $<

install:
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
	cat debian/changelog.base | etc/gitchangelog kno-imagetools > debian/changelog

debian/changelog: debian imagick.c qrcode.c exif.c makefile
	cat debian/changelog.base | etc/gitchangelog kno-imagetools > $@

debian.built: imagick.c qrcode.c exif.c makefile debian debian/changelog
	dpkg-buildpackage -sa -us -uc -b -rfakeroot && \
	touch $@

debian.signed: debian.built
	debsign --re-sign -k${GPGID} ../kno-imagetools_*.changes && \
	touch $@

debian.updated: debian.signed
	dupload -c ./debian/dupload.conf --nomail --to bionic ../kno-imagetools_*.changes && touch $@

update-apt: debian.updated

debclean:
	rm -f ../kno-imagetools_* ../kno-imagetools-* debian/changelog

debfresh:
	make debclean
	make debian.built
