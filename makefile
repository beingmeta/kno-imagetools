KNOCONFIG         = knoconfig
KNOBUILD          = knobuild

prefix		::= $(shell ${KNOCONFIG} prefix)
libsuffix	::= $(shell ${KNOCONFIG} libsuffix)
KNO_CFLAGS	::= -I. -fPIC $(shell ${KNOCONFIG} cflags)
KNO_LDFLAGS	::= -fPIC $(shell ${KNOCONFIG} ldflags)
KNO_LIBS	::= $(shell ${KNOCONFIG} libs)
PACKAGE_CFLAGS  ::= $(shell etc/pkc --cflags libexif) \
		    $(shell etc/pkc --cflags libqrencode) \
		    $(shell etc/pkc --cflags libpng) \
		    $(shell etc/pkc --cflags ImageMagick) \
		    $(shell etc/pkc --cflags MagickWand)
PACKAGE_LDFLAGS ::= $(shell etc/pkc --libs libexif) \
		    $(shell etc/pkc --libs libqrencode) \
		    $(shell etc/pkc --libs libpng) \
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
PKG_VERSION     ::= $(shell u8_gitversion ./etc/knomod_version)
PKG_MAJOR       ::= $(shell cat ./etc/knomod_version | cut -d. -f1)
FULL_VERSION    ::= ${KNO_MAJOR}.${KNO_MINOR}.${PKG_VERSION}

PKG_NAME	::= imagetools

SUDO            ::= $(shell which sudo)

MKSO		  = $(CC) -shared $(CFLAGS) $(LDFLAGS) $(LIBS)
MSG		  = echo
SYSINSTALL        = /usr/bin/install -c
MACLIBTOOL	  = $(CC) -dynamiclib -single_module -undefined dynamic_lookup \
			$(LDFLAGS)

GPGID             = FE1BC737F9F323D732AA26330620266BE5AFF294
CODENAME	::= $(shell ${KNOCONFIG} codename)
REL_BRANCH	::= $(shell ${KNOBUILD} getbuildopt REL_BRANCH current)
REL_STATUS	::= $(shell ${KNOBUILD} getbuildopt REL_STATUS stable)
REL_PRIORITY	::= $(shell ${KNOBUILD} getbuildopt REL_PRIORITY medium)
ARCH            ::= $(shell ${KNOBUILD} getbuildopt BUILD_ARCH || uname -m)
APKREPO         ::= $(shell ${KNOBUILD} getbuildopt APKREPO /srv/repo/kno/apk)
APK_ARCH_DIR      = ${APKREPO}/staging/${ARCH}
RPMDIR		  = dist

default build: qrcode.${libsuffix} exif.${libsuffix} imagick.${libsuffix}

%.o: %.c
	$(CC) $(CFLAGS) -D_FILEINFO="\"$(shell u8_fileinfo ./$< $(dirname $(pwd))/)\"" -o $@ -c $<
	@$(MSG) CC $@ $<
%.so: %.o
	$(MKSO) $(LDFLAGS) -o $@ $^ ${LDFLAGS}
	@$(MSG) MKSO  $@ $<
	@ln -sf $(@F) $(@D)/$(@F).${KNO_MAJOR}
%.dylib: %.o makefile
	@$(MACLIBTOOL) -install_name \
		`basename $(@F) .dylib`.${KNO_MAJOR}.dylib \
		${CFLAGS} ${LDFLAGS} -o $@ $(DYLIB_FLAGS) \
		$<
	@$(MSG) MACLIBTOOL  $@ $<

TAGS: exif.c qrcode.c imagick.c
	etags -o TAGS $<

${CMODULES}:
	install -d $@

install: build ${CMODULES}
	@for mod_name in qrcode exif imagick; do \
	  ${SUDO} u8_install_shared $${mod_name}.${libsuffix} ${CMODULES} ${FULL_VERSION} "${SYSINSTALL}"; \
	done;

clean:
	rm -f *.o *.${libsuffix}
fresh:
	make clean
	make default

gitup gitup-trunk:
	git checkout trunk && git pull

# RPM packaging

dist/kno-${PKG_NAME}.spec: dist/kno-${PKG_NAME}.spec.in makefile
	u8_xsubst dist/kno-${PKG_NAME}.spec dist/kno-${PKG_NAME}.spec.in \
		"VERSION" "${FULL_VERSION}" \
		"PKG_NAME" "${PKG_NAME}" && \
	touch $@
kno-${PKG_NAME}.tar: dist/kno-${PKG_NAME}.spec
	git archive -o $@ --prefix=kno-${PKG_NAME}-${FULL_VERSION}/ HEAD
	tar -f $@ -r dist/kno-${PKG_NAME}.spec

dist/rpms.ready: kno-${PKG_NAME}.tar
	rpmbuild $(RPMFLAGS)  			\
	   --define="_rpmdir $(RPMDIR)"			\
	   --define="_srcrpmdir $(RPMDIR)" 		\
	   --nodeps -ta 				\
	    kno-${PKG_NAME}.tar && 	\
	touch dist/rpms.ready
dist/rpms.done: dist/rpms.ready
	@if (test "$(GPGID)" = "none" || test "$(GPGID)" = "" ); then 			\
	    touch dist/rpms.done;				\
	else 						\
	     echo "Enter passphrase for '$(GPGID)':"; 		\
	     rpm --addsign --define="_gpg_name $(GPGID)" 	\
		--define="__gpg_sign_cmd $(RPMGPG)"		\
		$(RPMDIR)/kno-${PKG_NAME}-${FULL_VERSION}*.src.rpm 		\
		$(RPMDIR)/*/kno*-@KNO_VERSION@-*.rpm; 	\
	fi && touch dist/rpms.done;
	@ls -l $(RPMDIR)/kno-${PKG_NAME}-${FULL_VERSION}-*.src.rpm \
		$(RPMDIR)/*/kno*-${FULL_VERSION}-*.rpm;

rpms: dist/rpms.done

cleanrpms:
	rm -rf dist/rpms.done dist/rpms.ready kno-${PKG_NAME}.tar dist/kno-${PKG_NAME}.spec

rpmupdate update-rpms freshrpms: cleanrpms
	make cleanrpms
	make -s dist/rpms.done

dist/rpms.installed: dist/rpms.done
	sudo rpm -Uvh ${RPMDIR}/*.rpm && sudo rpm -Uvh ${RPMDIR}/${ARCH}/*.rpm && touch $@

installrpms install-rpms: dist/rpms.installed

# Alpine packaging

staging/alpine:
	@install -d $@

staging/alpine/APKBUILD: dist/alpine/APKBUILD staging/alpine
	cp dist/alpine/APKBUILD staging/alpine

staging/alpine/kno-${PKG_NAME}.tar: staging/alpine
	git archive --prefix=kno-${PKG_NAME}/ -o staging/alpine/kno-${PKG_NAME}.tar HEAD

dist/alpine.setup: staging/alpine/APKBUILD makefile ${STATICLIBS} \
	staging/alpine/kno-${PKG_NAME}.tar
	if [ ! -d ${APK_ARCH_DIR} ]; then mkdir -p ${APK_ARCH_DIR}; fi && \
	( cd staging/alpine; \
		abuild -P ${APKREPO} clean cleancache cleanpkg && \
		abuild checksum ) && \
	touch $@

dist/alpine.done: dist/alpine.setup
	( cd staging/alpine; abuild -P ${APKREPO} ) && touch $@
dist/alpine.installed: dist/alpine.setup
	( cd staging/alpine; abuild -i -P ${APKREPO} ) && touch dist/alpine.done && touch $@


alpine: dist/alpine.done
install-alpine: dist/alpine.done

.PHONY: alpine

