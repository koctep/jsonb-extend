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
#include <utils/lsyscache.h>
#include <access/tupmacs.h>

PG_MODULE_MAGIC;

/* export */
PG_FUNCTION_INFO_V1(jsonb_extend);
PG_FUNCTION_INFO_V1(jsonb_deep_extend);

/* internal functions */
JsonbParseState *JsonbCopyObjectValues(JsonbParseState *state, Jsonb *object, bool copyToken);
JsonbParseState *JsonbCopyIteratorValues(JsonbParseState *state, JsonbIterator *it, bool copyToken);
int jsonb_inc(JsonbIterator **it, JsonbValue *val, bool skipNested, int *lvl);
int jsonb_key_cmp(JsonbValue *val1, JsonbValue *val2);
JsonbValue *pushJsonbValue1(JsonbParseState **pstate, JsonbIteratorToken seq, JsonbValue *scalarVal);

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
  bool            variadic= get_fn_expr_variadic(fcinfo->flinfo);
  ArrayType       *arr    = variadic && (PG_NARGS() > 0) ? PG_GETARG_ARRAYTYPE_P(0) : NULL;
  int             nargs   = variadic ? ArrayGetNItems(ARR_NDIM(arr), ARR_DIMS(arr)) : PG_NARGS(),
                  i,
                  type;
  int16           typlen;
  char            typalign;
  bool            typbyval,
                  copyToken= false;
  Jsonb           *first,
                  *current;
  JsonbValue      *res;
  JsonbParseState *state  = NULL;

  if (nargs == 0)
    PG_RETURN_NULL();

  if (variadic) {
    get_typlenbyvalalign(ARR_ELEMTYPE(arr), &typlen, &typbyval, &typalign);
    first = (Jsonb *) ARR_DATA_PTR(arr);
  } else
    first = PG_GETARG_JSONB(0);

  if (nargs == 1)
    PG_RETURN_JSONB(first);

  /* Scalar as first object may be useful only if we want to prepend an array */
  if (JB_ROOT_IS_SCALAR(first))
    elog(ERROR, "jsonb_extend: cannot extend scalar");

  type = JB_ROOT_IS_OBJECT(first) ? WJB_BEGIN_OBJECT : WJB_BEGIN_ARRAY;
  pushJsonbValue1(&state, type, NULL);

  for (i = 0; i < nargs; i++)
  {
    if (i == 0)
      current = first;
    else {
      if (variadic) {
        current = (Jsonb *)att_addlength_pointer((char *) current, typlen, (char *) current);
        current = (Jsonb *)att_align_nominal((char *)current, typalign);
      } else {
        current = PG_GETARG_JSONB(i);
      }
    }

    if (JB_ROOT_IS_OBJECT(first) && !JB_ROOT_IS_OBJECT(current))
      elog(ERROR, "jsonb_extend: object should be extended by object");

    copyToken = (i != 0) && JB_ROOT_IS_ARRAY(first) && JB_ROOT_IS_OBJECT(current);

    state = JsonbCopyObjectValues(state, current, copyToken);
  }

  type = JB_ROOT_IS_OBJECT(first) ? WJB_END_OBJECT : WJB_END_ARRAY;
  res = pushJsonbValue1(&state, type, NULL);
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
    pushJsonbValue1(&state, r, &val);

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
        pushJsonbValue1(&state, r, &val);
        break;
      case WJB_BEGIN_OBJECT:
      case WJB_BEGIN_ARRAY:
        lvl++;
        pushJsonbValue1(&state, r, &val);
        break;
      case WJB_END_OBJECT:
      case WJB_END_ARRAY:
        lvl--;
        pushJsonbValue1(&state, r, &val);
        break;
      default:
        ereport(WARNING, (errcode(ERRCODE_INVALID_PARAMETER_VALUE),
              errmsg("unparsed type %d", val.type)));
    }
  }
  if (copyToken && (r != WJB_DONE))
    pushJsonbValue1(&state, r, &val);
  if (r != WJB_DONE)
    r = JsonbIteratorNext(&it, &val, skipNested);
  if (r != WJB_DONE)
    elog(ERROR, "jsonb_extend: last token should be WJB_DONE");
  return state;
}

Datum
jsonb_deep_extend(PG_FUNCTION_ARGS)
{
  int skipNested = PG_GETARG_BOOL(0);
  int nargs = PG_NARGS() - 1;
  Jsonb *first = PG_GETARG_JSONB(1),
        *object;
  JsonbValue val[nargs];
  JsonbIterator *it[nargs];
  int r[nargs], current = 0, i, cl = 0;
  int lvl[nargs];
  JsonbParseState *state = NULL;
  JsonbValue *res, key;

  if (!JB_ROOT_IS_OBJECT(first))
    elog(ERROR, "jsonb_deep_extend: implemented only for objects");

  for (i = 0; i < nargs; i++) {
    lvl[i] = 0;
    object = i == 0 ? first : PG_GETARG_JSONB(i + 1);
    it[i] = JsonbIteratorInit(&(object->root));
    r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
    r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
  }
  res = pushJsonbValue1(&state, WJB_BEGIN_OBJECT, NULL);

  while(true)
  {
    for (i = 0, current = 0; i < nargs; i++) {
      while ((r[i] != WJB_KEY) && (r[i] != WJB_DONE))
        r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
      if ((lvl[i] == lvl[current] && r[i] == WJB_KEY && jsonb_key_cmp(&val[i], &val[current]) <= 0)
          || (lvl[i] > lvl[current])
          || (r[current] == WJB_DONE))
        current = i;
    }
    if (cl > lvl[current])
      for (; cl > lvl[current]; cl--)
        res = pushJsonbValue1(&state, WJB_END_OBJECT, NULL);
    if (r[current] == WJB_DONE)
      break;

    cl = lvl[current];
    key.val.string.len = val[current].val.string.len;
    key.val.string.val = palloc(key.val.string.len);
    strncpy(key.val.string.val, val[current].val.string.val, key.val.string.len);
    res = pushJsonbValue1(&state, r[current], &val[current]);
    for (i = 0; i < nargs; i++)
      if (cl == lvl[i] && jsonb_key_cmp(&key, &val[i]) == 0)
        r[i] = jsonb_inc(&it[i], &val[i], skipNested, &lvl[i]);
    if (r[current] == WJB_BEGIN_OBJECT) {
      res = pushJsonbValue1(&state, WJB_BEGIN_OBJECT, NULL);
      r[current] = jsonb_inc(&it[current], &val[current], skipNested, &lvl[current]);
      for (i = 0; i < nargs; i++) {
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
        res = pushJsonbValue1(&state, r[current], &val[current]);
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

JsonbValue *
pushJsonbValue1(JsonbParseState **pstate, JsonbIteratorToken seq,
			   JsonbValue *scalarVal)
{
  if (seq == WJB_BEGIN_ARRAY || seq == WJB_BEGIN_OBJECT || seq == WJB_END_ARRAY || seq == WJB_END_OBJECT)
    return pushJsonbValue(pstate, seq, NULL);
  return pushJsonbValue(pstate, seq, scalarVal);
}
