/*-------------------------------------------------------------------------
 *
 * jsonb_extend.c
 *	  Jsonb extend function
 *
 * Copyright (c) 2014, Ilya Ashchepkov
 *
 *
 *-------------------------------------------------------------------------
 */
#include <postgres.h>

#include <utils/jsonb.h>

PG_MODULE_MAGIC;

PG_FUNCTION_INFO_V1(jsonb_extend);

JsonbParseState *JsonbCopyValues(JsonbParseState *state, Jsonb *object, bool copyToken);

/*
 * JsonbCopyValue
 *
 * Creates new JsonbValue coping all entries from input objects/arrays/scalars
 * JsonbValueToJsonb provides "one key - one value, last value wins"
 */
Datum
jsonb_extend(PG_FUNCTION_ARGS)
{
  int nargs = PG_NARGS(),
      i,
      type;

  Jsonb *first = PG_GETARG_JSONB(0),
        *object;

  JsonbValue *res;

  JsonbParseState *state = NULL;

  bool copyToken = false;

  /* Scalar as first object may be useful only if we want to prepend an array */
  if (JB_ROOT_IS_SCALAR(first))
    elog(ERROR, "jsonb_extend: cannot extend scalar");

  type = JB_ROOT_IS_OBJECT(first) ? WJB_BEGIN_OBJECT : WJB_BEGIN_ARRAY;
  pushJsonbValue(&state, type, NULL);

  for (i = 0; i < nargs; i++)
  {
    object = PG_GETARG_JSONB(i);

    if (JB_ROOT_IS_OBJECT(first) && !JB_ROOT_IS_OBJECT(object))
      elog(ERROR, "jsonb_extend: object should be extended by object");

    copyToken = (i != 0) && JB_ROOT_IS_ARRAY(first) && JB_ROOT_IS_OBJECT(object);

    state = JsonbCopyValues(state, object, copyToken);
  }

  type = JB_ROOT_IS_OBJECT(first) ? WJB_END_OBJECT : WJB_END_ARRAY;
  res = pushJsonbValue(&state, type, NULL);
  PG_RETURN_JSONB(JsonbValueToJsonb(res));
}

JsonbParseState*
JsonbCopyValues(JsonbParseState *state, Jsonb *object, bool copyToken)
{
  JsonbValue val;
  JsonbIterator *it;
  int skipNested = false;
  int lvl = 0;
  int r;

  it = JsonbIteratorInit(&(object->root));
  r = JsonbIteratorNext(&it, &val, skipNested);

  if (copyToken)
    pushJsonbValue(&state, r, &val);

  for (r = JsonbIteratorNext(&it, &val, skipNested);
        lvl != 0 || (r != WJB_END_OBJECT && r != WJB_END_ARRAY);
        r = JsonbIteratorNext(&it, &val, skipNested))
  {
    switch (r) {
      case WJB_ELEM:
      case WJB_KEY:
      case WJB_VALUE:
        pushJsonbValue(&state, r, &val);
        break;
      case WJB_BEGIN_OBJECT:
      case WJB_BEGIN_ARRAY:
        lvl++;
        pushJsonbValue(&state, r, &val);
        break;
      case WJB_END_OBJECT:
      case WJB_END_ARRAY:
        lvl--;
        pushJsonbValue(&state, r, &val);
        break;
      default:
        ereport(WARNING, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
              errmsg("unparsed type %d", val.type)));
    }
  }
  if (copyToken)
    pushJsonbValue(&state, r, &val);
  r = JsonbIteratorNext(&it, &val, skipNested);
  if (r != WJB_DONE)
    elog(ERROR, "jsonb_extend: last token should be WJB_DONE");
  return state;
}
