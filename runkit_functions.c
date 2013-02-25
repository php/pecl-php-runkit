/*
  +----------------------------------------------------------------------+
  | PHP Version 5                                                        |
  +----------------------------------------------------------------------+
  | Copyright (c) 1997-2006 The PHP Group                                |
  +----------------------------------------------------------------------+
  | This source file is subject to version 3.0 of the PHP license,       |
  | that is bundled with this package in the file LICENSE, and is        |
  | available through the world-wide-web at the following url:           |
  | http://www.php.net/license/3_0.txt.                                  |
  | If you did not receive a copy of the PHP license and are unable to   |
  | obtain it through the world-wide-web, please send a note to          |
  | license@php.net so we can mail you a copy immediately.               |
  +----------------------------------------------------------------------+
  | Author: Sara Golemon <pollita@php.net>                               |
  +----------------------------------------------------------------------+
*/

/* $Id$ */

#include "php_runkit.h"

#ifdef PHP_RUNKIT_MANIPULATION
void php_runkit_function_dtor(zend_function *fe TSRMLS_DC) {
	zend_op_array *op = (zend_op_array*)fe;

	if (fe->type != ZEND_USER_FUNCTION) {
		destroy_zend_function(fe ZE2_TSRMLS_CC);
		return;
	}

	if (*op->refcount > 1) {
		--*op->refcount;
		return;
	}

	destroy_zend_function(fe ZE2_TSRMLS_CC);
}

int php_runkit_function_delete(HashTable *ht, const char *key, int key_len, ulong key_hash TSRMLS_DC) {
	zend_op_array *fe;
	HashTable *sv;
#ifdef ZEND_ENGINE_2_4
	void **rtc;
#endif

	if (key_hash == 0) {
		key_hash = zend_get_hash_value(key, key_len);
	}

	if (zend_hash_quick_find(ht, key, key_len, key_hash, (void**)&fe) == FAILURE) {
		/* Nothing to do */
		return SUCCESS;
	}

	if ((fe->type != ZEND_USER_FUNCTION) ||
	    (*fe->refcount <= 1)) {
		/* Internal func or last ref, nothing to work around */
		return zend_hash_quick_del(ht, key, key_len, key_hash);
	}

	sv = fe->static_variables;
	fe->static_variables = NULL;
#ifdef ZEND_ENGINE_2_4
	rtc = fe->run_time_cache;
	fe->run_time_cache = NULL;
#endif
	if (FAILURE == zend_hash_quick_del(ht, key, key_len, key_hash)) {
		return FAILURE;
	}
	fe->static_variables = sv;
#ifdef ZEND_ENGINE_2_4
	fe->run_time_cache = rtc;
#endif
	return SUCCESS;
}

/* {{{ php_runkit_check_call_stack
 */
int php_runkit_check_call_stack(zend_op_array *op_array TSRMLS_DC)
{
	zend_execute_data *ptr;

	ptr = EG(current_execute_data);

	while (ptr) {
		if (ptr->op_array && ptr->op_array->opcodes == op_array->opcodes) {
			return FAILURE;
		}
		ptr = ptr->prev_execute_data;
	}

	return SUCCESS;
}
/* }}} */

static void php_runkit_hash_key_dtor(zend_hash_key *hash_key)
{
	efree((void*)hash_key->arKey);
}

/* Maintain order */
#define PHP_RUNKIT_FETCH_FUNCTION_INSPECT	0
#define PHP_RUNKIT_FETCH_FUNCTION_REMOVE	1
#define PHP_RUNKIT_FETCH_FUNCTION_RENAME	2

/* {{{ php_runkit_fetch_function
 */
static int php_runkit_fetch_function(char *fname, int fname_len, zend_function **pfe, int flag TSRMLS_DC)
{
	zend_function *fe;

	php_strtolower(fname, fname_len);

	if (zend_hash_find(EG(function_table), fname, fname_len + 1, (void**)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() not found", fname);
		return FAILURE;
	}

	if (fe->type == ZEND_INTERNAL_FUNCTION &&
		!RUNKIT_G(internal_override)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() is an internal function and runkit.internal_override is disabled", fname);
		return FAILURE;
	}

	if (fe->type != ZEND_USER_FUNCTION &&
		fe->type != ZEND_INTERNAL_FUNCTION) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() is not a user or normal internal function", fname);
		return FAILURE;
	}

	if (pfe) {
		*pfe = fe;
	}

	if (fe->type == ZEND_INTERNAL_FUNCTION &&
		flag >= PHP_RUNKIT_FETCH_FUNCTION_REMOVE) {
		if (!RUNKIT_G(replaced_internal_functions)) {
			ALLOC_HASHTABLE(RUNKIT_G(replaced_internal_functions));
			zend_hash_init(RUNKIT_G(replaced_internal_functions), 4, NULL, NULL, 0);
		}
		zend_hash_add(RUNKIT_G(replaced_internal_functions), fname, fname_len + 1, (void*)fe, sizeof(zend_function), (void**)&fe);
		fe->common.prototype = fe;
		if (flag >= PHP_RUNKIT_FETCH_FUNCTION_RENAME) {
			zend_hash_key hash_key;

			if (!RUNKIT_G(misplaced_internal_functions)) {
				ALLOC_HASHTABLE(RUNKIT_G(misplaced_internal_functions));
				zend_hash_init(RUNKIT_G(misplaced_internal_functions), 4, NULL, (dtor_func_t)php_runkit_hash_key_dtor, 0);
			}
			hash_key.nKeyLength = fname_len + 1;
			hash_key.arKey = estrndup(fname, hash_key.nKeyLength);
			zend_hash_next_index_insert(RUNKIT_G(misplaced_internal_functions), (void*)&hash_key, sizeof(zend_hash_key), NULL);
		}
	}

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_function_copy_ctor
	Duplicate structures in an op_array where necessary to make an outright duplicate */
void php_runkit_function_copy_ctor(zend_function *fe, char *newname)
{
#ifdef ZEND_ENGINE_2_1
	zend_compiled_variable *dupvars;
#endif
#ifdef ZEND_ENGINE_2_4
	zend_literal *old_literals;
#endif
	zend_op *opcode_copy;
	int i;

	if (fe->op_array.static_variables) {
		HashTable *tmpHash;
		zval *tmpZval;

		ALLOC_HASHTABLE(tmpHash);
		zend_hash_init(tmpHash, 2, NULL, ZVAL_PTR_DTOR, 0);
		zend_hash_copy(tmpHash, fe->op_array.static_variables, (copy_ctor_func_t) zval_add_ref, (void*)&tmpZval, sizeof(zval*));
		fe->op_array.static_variables = tmpHash;
	}

	fe->op_array.refcount = emalloc(sizeof(zend_uint));
	*(fe->op_array.refcount) = 1;

#ifdef ZEND_ENGINE_2_1
	i = fe->op_array.last_var;
	dupvars = safe_emalloc(fe->op_array.last_var, sizeof(zend_compiled_variable), 0);
	while (i > 0) {
		i--;
		dupvars[i].name = estrdup(fe->op_array.vars[i].name);
		dupvars[i].name_len = fe->op_array.vars[i].name_len;
		dupvars[i].hash_value = fe->op_array.vars[i].hash_value;
	}
	fe->op_array.vars = dupvars;
#endif
#ifdef ZEND_ENGINE_2_4
{
	zend_literal *litcopy = safe_emalloc(fe->op_array.last_literal, sizeof(zend_literal), 0);
	for(i = 0; i < fe->op_array.last_literal; i++) {
		litcopy[i] = fe->op_array.literals[i];
		zval_copy_ctor(&(litcopy[i].constant));
	}
	old_literals = fe->op_array.literals;
	fe->op_array.literals = litcopy;
	fe->op_array.run_time_cache = NULL;
}
#endif

	opcode_copy = safe_emalloc(sizeof(zend_op), fe->op_array.last, 0);
	for(i = 0; i < fe->op_array.last; i++) {
		opcode_copy[i] = fe->op_array.opcodes[i];
		if (PHP_RUNKIT_OP_TYPE(opcode_copy[i].op1) == IS_CONST) {
#ifdef ZEND_ENGINE_2_4
			zend_literal *tmplit = (zend_literal*)fe->op_array.opcodes[i].op1.zv;
			opcode_copy[i].op1.zv = &fe->op_array.literals[tmplit - old_literals].constant;
#else
			zval_copy_ctor(&opcode_copy[i].op1.u.constant);
#endif
#ifdef ZEND_ENGINE_2
		} else {
			if (PHP_RUNKIT_OP_U(opcode_copy[i].op1).jmp_addr >= fe->op_array.opcodes &&
				PHP_RUNKIT_OP_U(opcode_copy[i].op1).jmp_addr <  fe->op_array.opcodes + fe->op_array.last) {
				PHP_RUNKIT_OP_U(opcode_copy[i].op1).jmp_addr =  opcode_copy + (PHP_RUNKIT_OP_U(fe->op_array.opcodes[i].op1).jmp_addr - fe->op_array.opcodes);
			}
#endif
		}

		if (PHP_RUNKIT_OP_TYPE(opcode_copy[i].op2) == IS_CONST) {
#ifdef ZEND_ENGINE_2_4
			zend_literal *tmplit = (zend_literal*)fe->op_array.opcodes[i].op2.zv;
			opcode_copy[i].op2.zv = &fe->op_array.literals[tmplit - old_literals].constant;
#else
			zval_copy_ctor(&opcode_copy[i].op2.u.constant);
#endif
#ifdef ZEND_ENGINE_2
		} else {
			if (PHP_RUNKIT_OP_U(opcode_copy[i].op2).jmp_addr >= fe->op_array.opcodes &&
				PHP_RUNKIT_OP_U(opcode_copy[i].op2).jmp_addr <  fe->op_array.opcodes + fe->op_array.last) {
				PHP_RUNKIT_OP_U(opcode_copy[i].op2).jmp_addr =  opcode_copy + (PHP_RUNKIT_OP_U(fe->op_array.opcodes[i].op2).jmp_addr - fe->op_array.opcodes);
			}
#endif
		}
	}

	fe->op_array.opcodes = opcode_copy;
#ifndef ZEND_ENGINE_2_4
	fe->op_array.start_op = fe->op_array.opcodes;
#endif

	if (newname) {
		fe->op_array.function_name = newname;
	} else {
		fe->op_array.function_name = estrdup(fe->op_array.function_name);
	}

#ifdef ZEND_ENGINE_2
	fe->op_array.prototype = fe;

	if (fe->op_array.arg_info) {
		zend_arg_info *tmpArginfo;

		tmpArginfo = safe_emalloc(sizeof(zend_arg_info), fe->op_array.num_args, 0);
		for(i = 0; i < fe->op_array.num_args; i++) {
			tmpArginfo[i] = fe->op_array.arg_info[i];
			tmpArginfo[i].name = estrndup(tmpArginfo[i].name, tmpArginfo[i].name_len);
			if (tmpArginfo[i].class_name) {
				tmpArginfo[i].class_name = estrndup(tmpArginfo[i].class_name, tmpArginfo[i].class_name_len);
			}
		}
		fe->op_array.arg_info = tmpArginfo;
	}

	fe->op_array.doc_comment = estrndup(fe->op_array.doc_comment, fe->op_array.doc_comment_len);
	fe->op_array.try_catch_array = (zend_try_catch_element*)estrndup((char*)fe->op_array.try_catch_array, sizeof(zend_try_catch_element) * fe->op_array.last_try_catch);
#endif

	fe->op_array.brk_cont_array = (zend_brk_cont_element*)estrndup((char*)fe->op_array.brk_cont_array, sizeof(zend_brk_cont_element) * fe->op_array.last_brk_cont);
}
/* }}}} */

ulong RUNKIT_TEMP_FUNCNAME_HASH = 0;

/* {{{ php_runkit_generate_lambda_method
	Heavily borrowed from ZEND_FUNCTION(create_function) */
int php_runkit_generate_lambda_method(char *arguments, int arguments_len, char *phpcode, int phpcode_len, zend_function **pfe TSRMLS_DC)
{
	char *eval_code, *eval_name;
	int eval_code_length;

	eval_code_length = sizeof("function " RUNKIT_TEMP_FUNCNAME) + arguments_len + 4 + phpcode_len;
	eval_code = (char*)emalloc(eval_code_length);
	snprintf(eval_code, eval_code_length, "function " RUNKIT_TEMP_FUNCNAME "(%s){%s}", arguments, phpcode);
	eval_name = zend_make_compiled_string_description("runkit runtime-created function" TSRMLS_CC);
	if (zend_eval_string(eval_code, NULL, eval_name TSRMLS_CC) == FAILURE) {
		efree(eval_code);
		efree(eval_name);
		return FAILURE;
	}
	efree(eval_code);
	efree(eval_name);

	if (zend_hash_find(EG(function_table), RUNKIT_TEMP_FUNCNAME, sizeof(RUNKIT_TEMP_FUNCNAME), (void **)pfe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_ERROR, "Unexpected inconsistency during create_function");
		return FAILURE;
	}

	return SUCCESS;
}
/* }}} */

/* {{{ php_runkit_destroy_misplaced_functions
	Wipe old internal functions that were renamed to new targets
	They'll get replaced soon enough */
int php_runkit_destroy_misplaced_functions(zend_hash_key *hash_key TSRMLS_DC)
{
	if (!hash_key->nKeyLength) {
		/* Nonsense, skip it */
		return ZEND_HASH_APPLY_REMOVE;
	}

	/* We know this is a ZEND_INTERNAL_FUNCTION, no need for zuf workaround */
	zend_hash_del(EG(function_table), hash_key->arKey, hash_key->nKeyLength);

	return ZEND_HASH_APPLY_REMOVE;
}
/* }}} */

static Bucket *php_runkit_hash_get_bucket(HashTable *ht, zend_hash_key *hash_key) {
	Bucket *p = ht->arBuckets[hash_key->h & ht->nTableMask];
	while (p) {
		if ((p->arKey == hash_key->arKey) ||
		    ((p->h == hash_key->h) && (p->nKeyLength == hash_key->nKeyLength) &&
		                              !memcmp(p->arKey, hash_key->arKey, hash_key->nKeyLength))) {
			return p;
		}
		p = p->pNext;
	}
	return NULL;
}

static void php_runkit_hash_move_to_front(HashTable *ht, Bucket *p) {
	if (!p) return;

	/* Unlink from global DLList */
	if (p->pListNext) {
		p->pListNext->pListLast = p->pListLast;
	}
	if (p->pListLast) {
		p->pListLast->pListNext = p->pListNext;
	}
	if (ht->pListTail == p) {
		ht->pListTail = p->pListLast;
	}
	if (ht->pListHead == p) {
		ht->pListHead = p->pListNext;
	}

	/* Relink at the front */
	p->pListLast = NULL;
	p->pListNext = ht->pListHead;
	if (p->pListNext) {
		p->pListNext->pListLast = p;
	}
	ht->pListHead = p;
	if (!ht->pListTail) {
		ht->pListTail = p;
	}
}

/* {{{ php_runkit_restore_internal_functions
	Cleanup after modifications to internal functions */
int php_runkit_restore_internal_functions(zend_internal_function *fe ZEND_HASH_APPLY_ARGS_TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key)
{
#ifndef ZEND_ENGINE_2_3
	void ***tsrm_ls = va_arg(args, void***); /* NULL when !defined(ZTS) */
#endif

	if (!hash_key->nKeyLength) {
		/* Nonsense, skip it */
		return ZEND_HASH_APPLY_REMOVE;
	}

	zend_hash_update(EG(function_table), hash_key->arKey, hash_key->nKeyLength, (void*)fe, sizeof(zend_function), (void**)&fe);
	fe->prototype = fe;

	/* It's possible for restored internal functions to now be blocking a ZEND_USER_FUNCTION
	 * which will screw up post-request cleanup.
	 * Avoid this by restoring internal functions to the front of the list where they won't be in the way
	 */
	php_runkit_hash_move_to_front(EG(function_table), php_runkit_hash_get_bucket(EG(function_table), hash_key));

	return ZEND_HASH_APPLY_REMOVE;
}
/* }}} */

/* *****************
   * Functions API *
   ***************** */

/* {{{ proto bool runkit_function_add(string funcname, string arglist, string code)
	Add a new function, similar to create_function, but allows specifying name
	There's nothing about this function that's better than eval(), it's here for completeness */
PHP_FUNCTION(runkit_function_add)
{
	char *funcname, *arglist, *code;
	int funcname_len, arglist_len, code_len;
	char *delta = NULL, *delta_desc;
	int retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/ss",
			&funcname, &funcname_len, &arglist, &arglist_len, &code, &code_len) == FAILURE) {
		RETURN_FALSE;
	}

	php_strtolower(funcname, funcname_len);

	if (zend_hash_exists(EG(function_table), funcname, funcname_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Function %s() already exists", funcname);
		RETURN_FALSE;
	}

	spprintf(&delta, 0, "function %s(%s){%s}", funcname, arglist, code);

	if (!delta) {
		RETURN_FALSE;
	}

	delta_desc = zend_make_compiled_string_description("runkit created function" TSRMLS_CC);
	retval = zend_eval_string(delta, NULL, delta_desc TSRMLS_CC);
	efree(delta_desc);
	efree(delta);

	RETURN_BOOL(retval == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_remove(string funcname)
 */
PHP_FUNCTION(runkit_function_remove)
{
	char *funcname;
	int funcname_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/", &funcname, &funcname_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(funcname, funcname_len, NULL, PHP_RUNKIT_FETCH_FUNCTION_REMOVE TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	RETURN_BOOL(php_runkit_function_delete(EG(function_table), funcname, funcname_len + 1, 0 TSRMLS_CC) == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_rename(string funcname, string newname)
 */
PHP_FUNCTION(runkit_function_rename)
{
	zend_function *fe, func;
	char *sfunc, *dfunc;
	int sfunc_len, dfunc_len;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/",
			&sfunc, &sfunc_len, &dfunc, &dfunc_len) == FAILURE) {
		RETURN_FALSE;
	}

	php_strtolower(dfunc, dfunc_len);

	if (zend_hash_exists(EG(function_table), dfunc, dfunc_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() already exists", dfunc);
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(sfunc, sfunc_len, &fe, PHP_RUNKIT_FETCH_FUNCTION_RENAME TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (fe->type == ZEND_USER_FUNCTION) {
		func = *fe;
		php_runkit_function_copy_ctor(&func, NULL);
	}

	if (php_runkit_function_delete(EG(function_table), sfunc, sfunc_len + 1, 0 TSRMLS_CC) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error removing reference to old function name %s()", sfunc);
		php_runkit_function_dtor(&func TSRMLS_CC);
		RETURN_FALSE;
	}

	if (func.type == ZEND_USER_FUNCTION) {
		efree((void*)func.common.function_name);
		func.common.function_name = estrndup(dfunc, dfunc_len);
	}

	if (zend_hash_add(EG(function_table), dfunc, dfunc_len + 1, &func, sizeof(zend_function), (void**)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add reference to new function name %s()", dfunc);
		php_runkit_function_dtor(fe TSRMLS_CC);
		RETURN_FALSE;
	}
	fe->common.prototype = fe;

	RETURN_TRUE;
}
/* }}} */

/* {{{ proto bool runkit_function_redefine(string funcname, string arglist, string code)
 */
PHP_FUNCTION(runkit_function_redefine)
{
	char *funcname, *arglist, *code;
	int funcname_len, arglist_len, code_len;
	char *delta = NULL, *delta_desc;
	int retval;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/ss",
			&funcname, &funcname_len,
			&arglist, &arglist_len,
			&code, &code_len) == FAILURE) {
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(funcname, funcname_len, NULL, PHP_RUNKIT_FETCH_FUNCTION_REMOVE TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (zend_hash_del(EG(function_table), funcname, funcname_len + 1) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to remove old function definition for %s()", funcname);
		RETURN_FALSE;
	}

	spprintf(&delta, 0, "function %s(%s){%s}", funcname, arglist, code);

	if (!delta) {
		RETURN_FALSE;
	}

	delta_desc = zend_make_compiled_string_description("runkit created function" TSRMLS_CC);
	retval = zend_eval_string(delta, NULL, delta_desc TSRMLS_CC);
	efree(delta_desc);
	efree(delta);

	RETURN_BOOL(retval == SUCCESS);
}
/* }}} */

/* {{{ proto bool runkit_function_copy(string funcname, string targetname)
 */
PHP_FUNCTION(runkit_function_copy)
{
	char *sfunc, *dfunc;
	int sfunc_len, dfunc_len;
	zend_function *fe, fecopy;

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/",
			&sfunc, &sfunc_len, &dfunc, &dfunc_len) == FAILURE) {
		RETURN_FALSE;
	}

	php_strtolower(dfunc, dfunc_len);

	if (zend_hash_exists(EG(function_table), dfunc, dfunc_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s() already exists", dfunc);
		RETURN_FALSE;
	}

	if (php_runkit_fetch_function(sfunc, sfunc_len, &fe, PHP_RUNKIT_FETCH_FUNCTION_INSPECT TSRMLS_CC) == FAILURE) {
		RETURN_FALSE;
	}

	if (fe->type == ZEND_USER_FUNCTION) {
		fecopy = *fe;
		php_runkit_function_copy_ctor(&fecopy, NULL);
		fe = &fecopy;
	} else {
		zend_hash_key hash_key;

		hash_key.nKeyLength = dfunc_len + 1;
		hash_key.arKey = estrndup(dfunc, hash_key.nKeyLength);
		if (!RUNKIT_G(misplaced_internal_functions)) {
			ALLOC_HASHTABLE(RUNKIT_G(misplaced_internal_functions));
			zend_hash_init(RUNKIT_G(misplaced_internal_functions), 4, NULL, (dtor_func_t)php_runkit_hash_key_dtor, 0);
		}
		zend_hash_next_index_insert(RUNKIT_G(misplaced_internal_functions), (void*)&hash_key, sizeof(zend_hash_key), NULL);
	}

	if (zend_hash_add(EG(function_table), dfunc, dfunc_len + 1, fe, sizeof(zend_function), (void**)&fe) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add refernce to new function name %s()", dfunc);
		php_runkit_function_dtor(fe TSRMLS_CC);
		RETURN_FALSE;
	}
	fe->common.prototype = fe;

	RETURN_TRUE;

}
/* }}} */
#endif /* PHP_RUNKIT_MANIPULATION */

/* {{{ proto bool runkit_return_value_used(void)
Does the calling function do anything with our return value? */
PHP_FUNCTION(runkit_return_value_used)
{
	zend_execute_data *ptr = EG(current_execute_data)->prev_execute_data;

	if (!ptr) {
		/* main() */
		RETURN_FALSE;
	}

#ifdef ZEND_ENGINE_2_4
	RETURN_BOOL(!(ptr->opline->result_type & EXT_TYPE_UNUSED));
#else
	RETURN_BOOL(!(ptr->opline->result.u.EA.type & EXT_TYPE_UNUSED));
#endif
}
/* }}} */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

