include globals.mak

# Makefile.am

SOURCES=

if BUILD_TEST
TESTS_DIR=test
unittest:
	@(cd test; make)
	@echo ""
	@echo "NOTICE: Unitary tests successfully build"
	@echo "Go in the test directory to run them"
	@echo ""
else
unittest:
	@echo "Ring WARNING:"
	@echo " -- You need the cppunit devel package to compile the unitary tests."
endif

ACLOCAL_AMFLAGS = -I m4


#FIXME: this will change when libring is fixed
if HAVE_WIN32
SUBDIRS = bin
else
SUBDIRS = src ringtones man $(TESTS_DIR) doc bin
endif

EXTRA_DIST = m4/libtool.m4 \
			 m4/lt~obsolete.m4 \
			 m4/ltoptions.m4 \
			 m4/ltsugar.m4 \
			 m4/ltversion.m4 \
			 contrib/bootstrap \
			 contrib/src

coverage:
	$(AM_V_at)lcov --directory $(top_builddir) --capture --output-file dring-coverage.info
	$(MKDIR_P) html-output
	genhtml -o html-output dring-coverage.info

coverage-clean:
	$(AM_V_at)lcov --directory $(top_builddir) --zerocounters
	$(AM_V_at)rm -rf dring-coverage.info
	$(AM_V_at)rm -rf html-output
	$(AM_V_at)find -name '*.gcda' -exec rm -f {} +
	$(AM_V_at)find -name '*.gcno' -exec rm -f {} +

.PHONY: coverage coverage-clean