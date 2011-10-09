include Makefile.sets

# Building for Windows (opt/xmingw is for Gentoo):
#HOST=/opt/xmingw/bin/i386-mingw32msvc-
HOST=/usr/local/i386-mingw32-4.3.0/bin/i386-mingw32-
LDOPTS += -L/sw/lib
CPPFLAGS += -I/usr/local/i386-mingw32-4.3.0/include -I/sw/include

# Building for native:
#HOST=
#LDFLAGS += -pthread 

CXX=$(HOST)g++
CC=$(HOST)gcc
CPP=$(HOST)gcc

OPTIM=-O3
#OPTIM=-O0

VERSION=1.7.2

ARCHFILES=COPYING Makefile.sets progdesc.php \
          assemble.cc assemble.hh \
          insgen.cc \
          tristate \
          hash.hh \
          expr.cc expr.hh \
          insdata.cc insdata.hh \
          parse.cc parse.hh \
          object.cc object.hh \
          precompile.cc precompile.hh \
          warning.cc warning.hh \
          dataarea.cc dataarea.hh \
          main.cc \
          \
          disasm.cc \
          \
          o65.cc o65.hh relocdata.hh \
          o65linker.cc o65linker.hh \
          refer.cc refer.hh msginsert.hh \
          space.cc space.hh \
          romaddr.cc romaddr.hh \
          binpacker.hh binpacker.tcc \
          logfiles.hh \
          rangeset.hh rangeset.tcc range.hh \
          miscfun.hh miscfun.tcc \
          link.cc \
          \
          doc/o65.cc doc/o65.hh \
          doc/o65linker.cc doc/o65linker.hh \
          doc/dumpo65.cc doc/testi.cc \
          doc/ct-vwf8.a65 \
          doc/ct-moglogo.a65 \
          doc/bitness.a65

PREFIX=/usr/local
BINDIR=$(PREFIX)/bin

ARCHNAME=snescom-$(VERSION)
ARCHDIR=archives/

PROGS=snescom disasm sneslink

INSTALLPROGS=snescom sneslink
INSTALL=install

all: $(PROGS)

snescom: \
		assemble.o insdata.o \
		object.o dataarea.o \
		expr.o parse.o precompile.o \
		main.o \
		warning.o romaddr.o
	$(CXX) $(CXXFLAGS) -g -o $@ $^ $(LDFLAGS)

sneslink: \
		link.o o65.o o65linker.o space.o refer.o romaddr.o \
		object.o dataarea.o \
		warning.o 
	$(CXX) $(CXXFLAGS) -g -o $@ $^ $(LDFLAGS)

disasm: disasm.o romaddr.o o65.o
	$(CXX) $(CXXFLAGS) -g -o $@ $^

clean: ;
	rm -f *.o $(PROGS)
distclean: clean
	rm -f *~ .depend
realclean: distclean


# Build a windows archive
fullzip: \
		snescom disasm sneslink \
		README.html      \
		README.TXT \
	          doc/o65.cc doc/o65.hh \
        	  doc/o65linker.cc doc/o65linker.hh \
	          doc/dumpo65.cc doc/testi.cc \
	          doc/ct-vwf8.a65 \
	          doc/ct-moglogo.a65 \
	          doc/bitness.a65
	@rm -rf $(ARCHNAME)
	- mkdir $(ARCHNAME){,/doc}
	for s in $^;do ln "$$s" $(ARCHNAME)/"$$s"; done
	for dir in . doc ; do (\
	 cd $(ARCHNAME)/$$dir; \
	 /bin/ls|while read s;do echo "$$s"|grep -qF . || test -d "$$s" || mv -v "$$s" "$$s".exe;done; \
	                           ); done
	$(HOST)strip $(ARCHNAME)/*.exe $(ARCHNAME)/*/*.exe
	- upx --overlay=strip -9 $(ARCHNAME)/*.exe $(ARCHNAME)/*/*.exe
	zip -r9 $(ARCHNAME)-win32.zip $(ARCHNAME)
	rm -rf $(ARCHNAME)
	mv -f $(ARCHNAME)-win32.zip archives/
	- ln -f archives/$(ARCHNAME)-win32.zip /WWW/src/arch/

#../assemble: ;
#	$(MAKE) -C ../.. utils/assemble
#
#%.o: %.cc ;
#	$(MAKE) -C ../.. utils/asm/$@

include depfun.mak

.PHONY: all clean distclean realclean
