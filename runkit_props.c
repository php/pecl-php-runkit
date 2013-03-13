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

#ifdef ZEND_ENGINE_2_4
# define DEFPROP_PI_CC , pi
#else
# define DEFPROP_PI_CC
#endif

/* {{{ php_runkit_update_children_def_props
	Scan the class_table for children of the class just updated */
int php_runkit_update_children_def_props(zend_class_entry *ce ZEND_HASH_APPLY_ARGS_TSRMLS_DC, int num_args, va_list args, zend_hash_key *hash_key)
{
	zend_class_entry *parent_class =  va_arg(args, zend_class_entry*);
	zval *p = va_arg(args, zval*);
	char *pname = va_arg(args, char*);
	int pname_len = va_arg(args, int);
#ifdef ZEND_ENGINE_2_4
	zend_property_info *pi = va_arg(args, zend_property_info*);
#endif
#ifndef ZEND_ENGINE_2_3
	TSRMLS_FETCH();
#endif

#ifdef ZEND_ENGINE_2
	ce = *((zend_class_entry**)ce);
#endif

	if (ce->parent != parent_class) {
		/* Not a child, ignore */
		return ZEND_HASH_APPLY_KEEP;
	}

	/* Process children of this child */
	zend_hash_apply_with_arguments(EG(class_table) ZEND_HASH_APPLY_ARGS_TSRMLS_CC, (apply_func_args_t)php_runkit_update_children_def_props, num_args, ce, p, pname, pname_len DEFPROP_PI_CC);

#ifdef ZEND_ENGINE_2_4
{
	Z_ADDREF_P(p);
	zend_property_info *oldpi;
	if (zend_hash_find(&ce->properties_info, pname, pname_len + 1, (void**)&oldpi) == SUCCESS) {
		zval_ptr_dtor(&(ce->default_properties_table[oldpi->offset]));
		ce->default_properties_table[oldpi->offset] = p;
	} else {
		zend_property_info newpi = *pi;
		newpi.name = estrndup(pi->name, pi->name_length);
		if (pi->doc_comment) newpi.doc_comment = estrndup(pi->doc_comment, pi->doc_comment_len);
		newpi.offset = ce->default_properties_count++;
		ce->default_properties_table = safe_erealloc(ce->default_properties_table, ce->default_properties_count, sizeof(zval*), 0);
		ce->default_properties_table[newpi.offset] = p;
		zend_hash_add(&ce->properties_info, pname, pname_len + 1, &newpi, sizeof(zend_property_info), (void**)&oldpi);
	}
}
#else /* PHP <= 5.3 */
	zend_hash_del(&ce->default_properties, pname, pname_len + 1);
	Z_ADDREF_P(p);
	if (zend_hash_add(&ce->default_properties, pname, pname_len + 1, p, sizeof(zval*), NULL) ==  FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Error updating child class");
		return ZEND_HASH_APPLY_KEEP;
	}
#endif

	return ZEND_HASH_APPLY_KEEP;
}
/* }}} */

/* {{{ php_runkit_prop_add
 */
static int php_runkit_def_prop_add(char *classname, int classname_len, char *propname, int propname_len, zval *value, int visibility TSRMLS_DC)
{
	zend_class_entry *ce;
	zval *copyval;
	char *temp, *key = propname;
	int temp_len, key_len = propname_len;

	switch (value->type) {
		case IS_LONG:
		case IS_DOUBLE:
		case IS_STRING:
		case IS_BOOL:
		case IS_RESOURCE:
		case IS_NULL:
			break;
		default:
			php_error_docref(NULL TSRMLS_CC, E_WARNING, "Default properties may only evaluate to scalar values");
			return FAILURE;
	}

	if (php_runkit_fetch_class(classname, classname_len, &ce TSRMLS_CC)==FAILURE) {
		return FAILURE;
	}

	if (ce->type != ZEND_USER_CLASS) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Adding properties to internal classes is not allowed");
		return FAILURE;
	}

	MAKE_STD_ZVAL(copyval);
	ZVAL_ZVAL(copyval, value, 1, 0);

#ifdef ZEND_ENGINE_2_4
{
	zend_property_info newpi = { 0 };
	if (zend_hash_exists(&ce->properties_info, key, key_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s already exists", classname, propname);
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}

	newpi.flags = (visibility == ZEND_ACC_PRIVATE) ? ZEND_ACC_PRIVATE : ((visibility == ZEND_ACC_PROTECTED) ? ZEND_ACC_PROTECTED : ZEND_ACC_PUBLIC);
	newpi.name = estrndup(key, key_len);
	newpi.name_length = key_len;
	newpi.h = zend_hash_func(key, key_len);
	newpi.offset = ce->default_properties_count++;
	ce->default_properties_table = safe_erealloc(ce->default_properties_table, ce->default_properties_count, sizeof(zval*), 0);
	ce->default_properties_table[newpi.offset] = copyval;
	newpi.doc_comment_len = spprintf((char**)&(newpi.doc_comment), 0, "Added by runkit from %s %s%s%s() line %ld",
	                                 EG(current_execute_data)->op_array->filename,
	                                 EG(current_execute_data)->op_array->scope ? EG(current_execute_data)->op_array->scope->name : "",
	                                 EG(current_execute_data)->op_array->scope ? "::" : "",
	                                 EG(current_execute_data)->op_array->function_name,
	                                 EG(current_execute_data)->opline->lineno);
	newpi.ce = ce;

	if (FAILURE == zend_hash_add(&ce->properties_info, key, key_len + 1, &newpi, sizeof(zend_property_info), NULL)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add default property to class definition");
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}
}
#else /* PHP <= 5.3 */
	/* Check for existing property by this name */
	/* Existing public? */
	if (zend_hash_exists(&ce->default_properties, key, key_len + 1)) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s already exists", classname, propname);
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}

#ifdef ZEND_ENGINE_2
	/* Existing Protected? */
	zend_mangle_property_name(&temp, &temp_len, "*", 1, propname, propname_len, 0);
	if (zend_hash_exists(&ce->default_properties, temp, temp_len + 1)) {
		efree(temp);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s already exists", classname, propname);
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}
	if (visibility == ZEND_ACC_PROTECTED) {
		key = temp;
		key_len = temp_len;
	} else {
		efree(temp);
	}

	/* Existing Private? */
	zend_mangle_property_name(&temp, &temp_len, classname, classname_len, propname, propname_len, 0);
	if (zend_hash_exists(&ce->default_properties, temp, temp_len + 1)) {
		efree(temp);
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "%s::%s already exists", classname, propname);
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}
	if (visibility == ZEND_ACC_PRIVATE) {
		key = temp;
		key_len = temp_len;
	} else {
		efree(temp);
	}
#endif /* ZEND_ENGINE_2 */

	if (zend_hash_add(&ce->default_properties, key, key_len + 1, &copyval, sizeof(zval *), NULL) == FAILURE) {
		php_error_docref(NULL TSRMLS_CC, E_WARNING, "Unable to add default property to class definition");
		zval_ptr_dtor(&copyval);
		return FAILURE;
	}
#endif /* ZEND_ENGINE_2_4 */

#ifdef ZEND_ENGINE_2
	if (visibility != ZEND_ACC_PRIVATE) {
		zend_hash_apply_with_arguments(EG(class_table) ZEND_HASH_APPLY_ARGS_TSRMLS_CC, (apply_func_args_t)php_runkit_update_children_def_props, 4, ce, copyval, key, key_len);
	}
#endif

	if (key != propname) {
		efree(key);
	}

	return SUCCESS;
}
/* }}} */

/* ******************
   * Properties API *
   ****************** */

/* {{{ proto bool runkit_default_property_add(string classname, string propname, mixed initialvalue[, int visibility])
Add a property to a class with a given visibility
 */
PHP_FUNCTION(runkit_default_property_add)
{
	char *classname, *propname;
	int classname_len, propname_len;
	zval *value;
	long visibility;
	int existing_visibility;

#ifdef ZEND_ENGINE_2
	visibility = ZEND_ACC_PUBLIC;
#endif

	if (zend_parse_parameters(ZEND_NUM_ARGS() TSRMLS_CC, "s/s/z|l", &classname, &classname_len, &propname, &propname_len, &value, &visibility) == FAILURE) {
		RETURN_FALSE;
	}

	php_strtolower(classname, classname_len);
	php_strtolower(propname, propname_len);

	RETURN_BOOL(php_runkit_def_prop_add(classname, classname_len, propname, propname_len, value, visibility TSRMLS_CC) == SUCCESS);
}
/* }}} */
#endif /* PHP_RUNKIT_MANIPULATION */

/*
 * Local variables:
 * tab-width: 4
 * c-basic-offset: 4
 * End:
 * vim600: noet sw=4 ts=4 fdm=marker
 * vim<600: noet sw=4 ts=4
 */

