# jsonb_extend/Makefile

MODULE_big = jsonb_extend
OBJS = jsonb_extend.o

EXTENSION = jsonb_extend
DATA = jsonb_extend--1.0.sql jsonb_extend--unpackaged--1.0.sql

REGRESS = jsonb_extend

ifdef USE_PGXS
PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
else
subdir = contrib/jsonb-extend
top_builddir = ../..
include $(top_builddir)/src/Makefile.global
include $(top_srcdir)/contrib/contrib-global.mk
endif
