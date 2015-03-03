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

/* export */
PG_FUNCTION_INFO_V1(jsonb_extend);
PG_FUNCTION_INFO_V1(jsonb_deep_extend);

/* internal functions */
JsonbParseState *JsonbCopyObjectValues(JsonbParseState *state, Jsonb *object, bool copyToken);
JsonbParseState *JsonbCopyIteratorValues(JsonbParseState *state, JsonbIterator *it, bool copyToken);
int jsonb_inc(JsonbIterator **it, JsonbValue *val, bool skipNested, int *lvl);
int jsonb_key_cmp(JsonbValue *val1, JsonbValue *val2);

/*
 * JsonbCopyValue
 *
 * Creates new JsonbValue coping all entries from input objects/arrays/scalars
 * JsonbValueToJsonb provides "one key - one value, last value wins"
 *
 ********** JavaScript code for objects
 * function jsonb_extend() {
 *  var res = {};
 *  var i = 0, len = arguments.length;
 *  var key;
 *
 *  for (i; i < len; i++)
 *    for (key in arguments[i])
 *      res[key] = arguments[i][key];
 *  return res;
 * }
 ********** End of JavaScript code
 */
Datum
jsonb_extend(PG_FUNCTION_ARGS)
{
  int nargs = PG_NARGS(),
      i,
      type;

  Jsonb *first = PG_GETARG_JSONB(0),
        *current;

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
    current = PG_GETARG_JSONB(i);

    if (JB_ROOT_IS_OBJECT(first) && !JB_ROOT_IS_OBJECT(current))
      elog(ERROR, "jsonb_extend: object should be extended by object");

    copyToken = (i != 0) && JB_ROOT_IS_ARRAY(first) && JB_ROOT_IS_OBJECT(current);

    state = JsonbCopyObjectValues(state, current, copyToken);
  }

  type = JB_ROOT_IS_OBJECT(first) ? WJB_END_OBJECT : WJB_END_ARRAY;
  res = pushJsonbValue(&state, type, NULL);
  PG_RETURN_JSONB(JsonbValueToJsonb(res));
}

JsonbParseState*
JsonbCopyObjectValues(JsonbParseState *state, Jsonb *object, bool copyToken)
{
  JsonbIterator *it;
  it = JsonbIteratorInit(&(object->root));
  return JsonbCopyIteratorValues(state, it, copyToken);
}

JsonbParseState*
JsonbCopyIteratorValues(JsonbParseState *state, JsonbIterator *it, bool copyToken)
{
  JsonbValue val;
  /* we going throught nested objects 'cause limitation of pushJsonbValue */
  int skipNested = false;
  int lvl = 0;
  int r;

  r = JsonbIteratorNext(&it, &val, skipNested);

  if (copyToken)
    pushJsonbValue(&state, r, &val);

  for (r = JsonbIteratorNext(&it, &val, skipNested);
        r != WJB_DONE &&
        (
          lvl != 0 || (r != WJB_END_OBJECT && r != WJB_END_ARRAY)
        );
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
  if (copyToken && (r != WJB_DONE))
    pushJsonbValue(&state, r, &val);
  if (r != WJB_DONE)
    r = JsonbIteratorNext(&it, &val, skipNested);
  if (r != WJB_DONE)
    elog(ERROR, "jsonb_extend: last token should be WJB_DONE");
  return state;
}

Datum
jsonb_deep_extend(PG_FUNCTION_ARGS)
{
  Jsonb *first = PG_GETARG_JSONB(0),
        *object;
  int skipNested = PG_GETARG_BOOL(2);
  JsonbValue val[2];
  JsonbIterator *it[2];
  int r[2], current = 0, i, cl = 0;
  //  int skipNested = false;
  int lvl[2] = {0, 0};
  JsonbParseState *state = NULL;
  JsonbValue *res, key;

  if (!JB_ROOT_IS_OBJECT(first))
    elog(ERROR, "jsonb_deep_extend: implemented only for objects");

  for (i = 0; i < 2; i++) {
    object = i == 0 ? first : PG_GETARG_JSONB(i);
    it[i] = JsonbIteratorInit(&(object->root));
    r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
    r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
  }
  res = pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);

  while(true)
  {
    for (i = 0, current = 0; i < 2; i++) {
      while ((r[i] != WJB_KEY) && (r[i] != WJB_DONE))
        r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
      if ((lvl[i] == lvl[current] && r[i] == WJB_KEY && jsonb_key_cmp(&val[i], &val[current]) <= 0)
          || (lvl[i] > lvl[current])
          || (r[current] == WJB_DONE))
        current = i;
    }
    if (cl > lvl[current])
      for (cl; cl > lvl[current]; cl--)
        res = pushJsonbValue(&state, WJB_END_OBJECT, NULL);
    if (r[current] == WJB_DONE)
      break;

    cl = lvl[current];
    key.val.string.len = val[current].val.string.len;
    key.val.string.val = palloc(key.val.string.len);
    strncpy(key.val.string.val, val[current].val.string.val, key.val.string.len);
    res = pushJsonbValue(&state, r[current], &val[current]);
    for (i = 0; i < 2; i++)
      if (cl == lvl[i] && jsonb_key_cmp(&key, &val[i]) == 0)
        r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
    if (r[current] == WJB_BEGIN_OBJECT) {
      res = pushJsonbValue(&state, WJB_BEGIN_OBJECT, NULL);
      r[current] = jsonb_inc(&it[current], &val[current], skipNested, &lvl[current]);
      for (i = 0; i < 2; i++) {
        if (r[i] == WJB_KEY) continue;
        if (lvl[i] < cl) continue;
        if (i < current)
          if (r[i] != WJB_BEGIN_OBJECT)
            while (true) {
              if (lvl[i] < cl) break;
              if (lvl[i] == cl && r[current] == WJB_KEY) break;
              r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
            }
      }
    } else {
      while (true) {
        if (lvl[current] < cl)
          break;
        if (lvl[current] == cl && r[current] == WJB_KEY)
          break;
        res = pushJsonbValue(&state, r[current], &val[current]);
        r[current] = jsonb_inc(&it[current], &val[current], skipNested, &lvl[current]);
      }
      for (i = 0; i < current; i++) {
        if (r[i] == WJB_KEY) continue;
        while(true) {
          if (lvl[i] < cl) break;
          if (lvl[i] == cl && r[i] == WJB_KEY) break;
          r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
        }
      }
    }
    pfree(key.val.string.val);
  }

  PG_RETURN_JSONB(JsonbValueToJsonb(res));
}

int
jsonb_inc(JsonbIterator **it, JsonbValue *val, bool skipNested, int *lvl)
{
  int r = JsonbIteratorNext(it, val, skipNested);
  if (r == WJB_BEGIN_OBJECT) {
    ++(*lvl);
  }
  if (r == WJB_END_OBJECT) {
    --(*lvl);
  }
  return r;
}

int
jsonb_key_cmp(JsonbValue *val1, JsonbValue *val2)
{
  if (val1->val.string.len < val2->val.string.len)
    return -1;
  if (val1->val.string.len > val2->val.string.len)
    return  1;
  return strncmp(val1->val.string.val, val2->val.string.val, val1->val.string.len);
}
