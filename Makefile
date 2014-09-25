# jsonb_extend/Makefile

MODULE_big = jsonb_extend
OBJS = jsonb_extend.o

EXTENSION = jsonb_extend
DATA = jsonb_extend--1.0.sql jsonb_extend--unpackaged--1.0.sql

REGRESS = jsonb_extend

PG_CONFIG = pg_config
PGXS := $(shell $(PG_CONFIG) --pgxs)
include $(PGXS)
