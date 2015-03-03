/* contrib/test_parser/test_parser--unpackaged--1.0.sql */

-- complain if script is sourced in psql, rather than via CREATE EXTENSION
\echo Use "CREATE EXTENSION jsonb_extend" to load this file. \quit

ALTER EXTENSION jsonb_extend ADD function jsonb_extend(jsonb,jsonb);
ALTER EXTENSION jsonb_extend ADD function jsonb_deep_extend(jsonb,jsonb,bool);
