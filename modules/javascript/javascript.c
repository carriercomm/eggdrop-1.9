/*
 * javascript.c --
 */
/*
 * Copyright (C) 2002 Eggheads Development Team
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef lint
static const char rcsid[] = "$Id: javascript.c,v 1.5 2002/05/07 22:09:08 stdarg Exp $";
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define XP_UNIX

#include "jsapi.h"

#include "lib/eggdrop/module.h"
#include "lib/egglib/mstack.h"
#include "lib/egglib/msprintf.h"
#include <eggdrop/eggdrop.h>

#define MODULE_NAME "javascript"

static eggdrop_t *egg = NULL;

/* Data we need for a JavaScript callback. */
typedef struct {
	JSContext *mycx;
	JSObject *myobj;
	JSFunction *command;
	char *name;
} my_callback_cd_t;

static int my_command_handler(JSContext *cx, JSObject *obj, int argc, jsval *argv, jsval *rval);

static int c_to_js_var(JSContext *cx, script_var_t *v, jsval *result);

static int js_to_c_var(JSContext *cx, JSObject *obj, jsval val, script_var_t *var, int type);

static JSRuntime *global_js_runtime;
static JSContext *global_js_context;
static JSObject  *global_js_object;

static char *error_logfile = NULL;

/* When Javascript tries to use one of our linked vars, it calls this function
	with "id" set to the property it's trying to look up. */
static int my_eggvar_getter(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
	char *str;
	script_linked_var_t *linked_var;

	linked_var = JS_GetPrivate(cx, obj);
	str = JS_GetStringBytes(JS_ValueToString(cx, id));

	putlog(LOG_MISC, "*", "my_getter: %s, %s", linked_var->name, str);
	return JS_TRUE;
}

static int my_eggvar_setter(JSContext *cx, JSObject *obj, jsval id, jsval *vp)
{
	char *str;
	script_linked_var_t *linked_var;

	linked_var = JS_GetPrivate(cx, obj);
	str = JS_GetStringBytes(JS_ValueToString(cx, id));

	putlog(LOG_MISC, "*", "my_setter: %s, %s", linked_var->name, str);
	return JS_FALSE;
}

static JSClass eggvar_class = {
	"EggdropVariable", JSCLASS_HAS_PRIVATE,
	JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
	JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub
};

static JSClass eggfunc_class = {
	"EggdropFunction", JSCLASS_HAS_PRIVATE,
	JS_PropertyStub, JS_PropertyStub, JS_PropertyStub, JS_PropertyStub,
	JS_EnumerateStub, JS_ResolveStub, JS_ConvertStub, JS_FinalizeStub,
	NULL, NULL,
	my_command_handler
};

static JSClass global_class = {"global", JSCLASS_HAS_PRIVATE,
	JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,JS_PropertyStub,
	JS_EnumerateStub,JS_ResolveStub,JS_ConvertStub,JS_FinalizeStub
};

/* Load a JS script. */
static int my_load_script(registry_entry_t *entry, char *fname)
{
	int result;
	int len;

	/* Check the filename and make sure it ends in .tcl */
	len = strlen(fname);
	if (len < 3 || fname[len-1] != 's' || fname[len-2] != 'j' || fname[len-3] != '.') {
		/* Nope, let someone else load it. */
		return(0);
	}

	/* result = Tcl_EvalFile(ginterp, fname); */
	return(0);
}

static void linked_var_to_jsval(JSContext *cx, script_linked_var_t *var, jsval *val)
{
	script_var_t script_var;

	script_var.type = var->type & SCRIPT_TYPE_MASK;
	script_var.len = -1;
	script_var.value = *(void **)var->value;

	c_to_js_var(cx, &script_var, val);
}

/* This gets called when people do, e.g. botnick.set("newnick"); */
static int my_eggvar_set(JSContext *cx, JSObject *obj, int argc, jsval *argv, jsval *rval)
{
	script_linked_var_t *linked_var;
	script_var_t script_var;

	if (argc != 1) return JS_FALSE;

	linked_var = JS_GetPrivate(cx, obj);
	if (!linked_var) {
		putlog(LOG_MISC, "*", "don't create your own eggvars!");
		return JS_FALSE;
	}

	js_to_c_var(cx, obj, argv[0], &script_var, linked_var->type);
	if (linked_var->callbacks && linked_var->callbacks->on_write) {
		int r;
		r = (linked_var->callbacks->on_write)(linked_var, &script_var);
		if (r) return JS_FALSE;
		c_to_js_var(cx, &script_var, rval);
		return JS_TRUE;
	}
	switch (linked_var->type & SCRIPT_TYPE_MASK) {
		case SCRIPT_UNSIGNED:
		case SCRIPT_INTEGER:
			*(int *)(linked_var->value) = (int) script_var.value;
			break;
		case SCRIPT_STRING: {
			char **charptr = (char **)(linked_var->value);

			if (*charptr) free(*charptr);
			*charptr = strdup(script_var.value);
			break;
		}
		default:
			return JS_FALSE;
	}
	c_to_js_val(cx, &script_var, rval);
	return JS_TRUE;
}


/* Get the value of the variable. */
static int my_eggvar_value_of(JSContext *cx, JSObject *obj, int argc, jsval *argv, jsval *rval)
{
	script_linked_var_t *linked_var;

	linked_var = JS_GetPrivate(cx, obj);
	if (linked_var->callbacks && linked_var->callbacks->on_read) {
		int r = (linked_var->callbacks->on_read)(linked_var);
		if (r) return JS_FALSE;
	}
	linked_var_to_jsval(cx, linked_var, rval);
	return(JS_TRUE);
}

/* This function creates the JS <-> C variable linkage on reads, writes, and unsets. */
static int my_link_var(void *ignore, script_linked_var_t *var)
{
	char *varname;
	JSObject *obj;

	if (var->class && strlen(var->class)) varname = msprintf("%s(%s)", var->class, var->name);
	else varname = strdup(var->name);

	obj = JS_DefineObject(global_js_context, global_js_object,
		varname, &eggvar_class, NULL, JSPROP_ENUMERATE|JSPROP_EXPORTED|JSPROP_READONLY);
	if (!obj) {
		putlog(LOG_MISC, "*", "failed 1");
		return(0);
	}
	JS_SetPrivate(global_js_context, obj, var);
	JS_DefineFunction(global_js_context, obj,
		"toString", my_eggvar_value_of, 0, 0);
	JS_DefineFunction(global_js_context, obj,
		"valueOf", my_eggvar_value_of, 0, 0);
	JS_DefineFunction(global_js_context, obj,
		"set", my_eggvar_set, 0, 0);
	return(0);
}

/* This function deletes JS <-> C variable links. */
static int my_unlink_var(void *ignore, script_linked_var_t *var)
{
	char *varname;

	if (var->class && strlen(var->class)) varname = msprintf("%s(%s)", var->class, var->name);
	else varname = strdup(var->name);

	JS_DeleteProperty(global_js_context, global_js_object, varname);

	free(varname);
	return(0);
}

/*
static void log_error_message(Tcl_Interp *myinterp)
{
	FILE *fp;
	char *errmsg;
	time_t timenow;

	errmsg = Tcl_GetStringResult(myinterp);
	putlog(LOG_MISC, "*", "Tcl Error: %s", errmsg);

	if (!error_logfile || !error_logfile[0]) return;

	timenow = time(NULL);
	fp = fopen(error_logfile, "a");
	if (!fp) putlog(LOG_MISC, "*", "Error opening Tcl error log (%s)!", error_logfile);
	else {
		errmsg = Tcl_GetVar(myinterp, "errorInfo", TCL_GLOBAL_ONLY);
		fprintf(fp, "%s", asctime(localtime(&timenow)));
		fprintf(fp, "%s\n\n", errmsg);
		fclose(fp);
	}
}
*/

/* When you use a script_callback_t's callback() function, this gets executed.
	It converts the C variables to JS variables and executes the JS script.
*/
static int my_js_callbacker(script_callback_t *me, ...)
{
	jsval *argv, result = 0;
	script_var_t var;
	my_callback_cd_t *cd; /* My callback client data */
	int i, n, retval;
	void **al;

	/* This struct contains the interp and the obj command. */
	cd = (my_callback_cd_t *)me->callback_data;

	al = (void **)&me;
	al++;
	if (me->syntax) n = strlen(me->syntax);
	else n = 0;

	argv = (jsval *)malloc(n * sizeof(jsval));
	for (i = 0; i < n; i++) {
		var.type = me->syntax[i];
		var.value = al[i];
		var.len = -1;
		c_to_js_var(cd->mycx, &var, argv+i);
	}

	n = JS_CallFunction(cd->mycx, cd->myobj, cd->command, n, argv, &result);
	free(argv);
	if (n) {
		int32 intval;

		if (JS_ValueToInt32(cd->mycx, result, &intval)) retval = intval;
	}

	/* If it's a one-time callback, delete it. */
	if (me->flags & SCRIPT_CALLBACK_ONCE) me->del(me);

	return(retval);
}

/* This implements the delete() member of JS script callbacks. */
static int my_js_cb_delete(script_callback_t *me)
{
	my_callback_cd_t *cd;

	cd = (my_callback_cd_t *)me->callback_data;
	free(cd->name);
	if (me->syntax) free(me->syntax);
	free(cd);
	free(me);
	return(0);
}

/* Create JS commands with a C callback. */
static int my_create_cmd(void *ignore, script_command_t *info)
{
	char *cmdname;
	//JSFunction *func;
	JSObject *obj;

	if (info->class && strlen(info->class)) {
		cmdname = msprintf("%s_%s", info->class, info->name);
	}
	else {
		cmdname = strdup(info->name);
	}

	obj = JS_DefineObject(global_js_context, global_js_object,
		cmdname, &eggfunc_class, NULL, JSPROP_ENUMERATE|JSPROP_EXPORTED|JSPROP_READONLY);

	if (!obj) return(0);

	//free(cmdname);

	//JS_DefineFunction(global_js_context, obj,
		//"toString", my_to_string, 0, 0);
	JS_SetPrivate(global_js_context, obj, info);

	return(0);
}

/* Delete JS commands. */
static int my_delete_cmd(void *ignore, script_command_t *info)
{
	char *cmdname;

	if (info->class && strlen(info->class)) {
		cmdname = msprintf("%s_%s", info->class, info->name);
	}
	else {
		cmdname = strdup(info->name);
	}
	JS_DeleteProperty(global_js_context, global_js_object, cmdname);
	free(cmdname);

	return(0);
}

/* Convert a C variable to a Tcl variable. */
static int c_to_js_var(JSContext *cx, script_var_t *v, jsval *result)
{
	*result = JSVAL_VOID;

	/* If it's an array, we call ourselves recursively. */
	if (v->type & SCRIPT_ARRAY) {
		JSObject *js_array;
		jsval element;
		int i;

		js_array = JS_NewArrayObject(cx, 0, NULL);

		/* If it's an array of script_var_t's, then it's easy. */
		if ((v->type & SCRIPT_TYPE_MASK) == SCRIPT_VAR) {
			script_var_t **v_list;

			v_list = (script_var_t **)v->value;
			for (i = 0; i < v->len; i++) {
				c_to_js_var(cx, v_list[i], &element);
				JS_SetElement(cx, js_array, i, &element);
			}
		}
		else {
			/* Otherwise, we have to turn them into fake script_var_t's. */
			script_var_t v_sub;
			void **values;

			v_sub.type = v->type & (~SCRIPT_ARRAY);
			v_sub.len = -1;
			values = (void **)v->value;
			for (i = 0; i < v->len; i++) {
				v_sub.value = values[i];
				c_to_js_var(cx, &v_sub, &element);
				JS_SetElement(cx, js_array, i, &element);
			}
		}
		/* Whew */
		if (v->type & SCRIPT_FREE) free(v->value);
		if (v->type & SCRIPT_FREE_VAR) free(v);
		*result = OBJECT_TO_JSVAL(js_array);
		return(0);
	}

	/* Here is where we handle the basic types. */
	switch (v->type & SCRIPT_TYPE_MASK) {
		case SCRIPT_INTEGER:
		case SCRIPT_UNSIGNED:
			*result = INT_TO_JSVAL(((int) v->value));
			break;
		case SCRIPT_STRING:
		case SCRIPT_BYTES: {
			char *str = v->value;

			if (!str) str = "";
			if (v->len == -1) v->len = strlen(str);
			*result = STRING_TO_JSVAL(JS_NewStringCopyN(cx, str, v->len));

			if (v->value && v->type & SCRIPT_FREE) free((char *)v->value);
			break;
		}
		case SCRIPT_POINTER: {
			char str[32];

			sprintf(str, "#%u", (unsigned int) v->value);
			*result = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, str));
			break;
		}
		case SCRIPT_USER: {
			/* An eggdrop user record (struct userrec *). */
			char *handle;
			struct userrec *u = (struct userrec *)v->value;

			if (u) handle = u->handle;
			else handle = "*";
			*result = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, handle));
			break;
		}
		default:
			/* Default: just pass a string with an error message. */
			*result = STRING_TO_JSVAL(JS_NewStringCopyZ(cx, "Unsupported type"));
	}
	if (v->type & SCRIPT_FREE_VAR) free(v);
	return(0);
}

/* Here we convert JS variables into C variables.
	When we return, var->type may have the SCRIPT_FREE bit set, in which case
	you should free(var->value) when you're done with it. */
static int js_to_c_var(JSContext *cx, JSObject *obj, jsval val, script_var_t *var, int type)
{
	int err = 0;

	var->type = type;
	var->len = -1;
	var->value = NULL;

	switch (type) {
		case SCRIPT_STRING: {
			JSString *str;

			str = JS_ValueToString(cx, val);
			var->value = JS_GetStringBytes(str);
			var->len = JS_GetStringLength(str);
			break;
		}
		case SCRIPT_BYTES: {
			byte_array_t *byte_array;
			JSString *str;

			byte_array = (byte_array_t *)malloc(sizeof(*byte_array));

			str = JS_ValueToString(cx, val);
			byte_array->bytes = JS_GetStringBytes(str);
			byte_array->len = JS_GetStringLength(str);

			var->value = byte_array;
			var->type |= SCRIPT_FREE;
			break;
		}
		case SCRIPT_UNSIGNED:
		case SCRIPT_INTEGER: {
			int32 intval;

			err = !JS_ValueToInt32(cx, val, &intval);
			var->value = (void *)intval;
			break;
		}
		case SCRIPT_CALLBACK: {
			JSFunction *func;
			script_callback_t *cback; /* Callback struct */
			my_callback_cd_t *cdata; /* Our client data */

			func = JS_ValueToFunction(cx, val);
			if (!func) {
				err = 1;
				break;
			}

			cback = (script_callback_t *)calloc(1, sizeof(*cback));
			cdata = (my_callback_cd_t *)calloc(1, sizeof(*cdata));
			cback->callback = (Function) my_js_callbacker;
			cback->callback_data = (void *)cdata;
			cback->del = (Function) my_js_cb_delete;
			cback->name = strdup(JS_GetFunctionName(func));

			cdata->mycx = cx;
			cdata->myobj = obj;
			cdata->command = func;

			var->value = cback;
			break;
		}
		case SCRIPT_USER: {
			struct userrec *u;
			char *handle;

			handle = JS_GetStringBytes(JS_ValueToString(cx, val));
			u = get_user_by_handle(userlist, handle);
			var->value = u;
			if (!u) {
				JS_ReportError(cx, "User not found: %s", handle);
				err = 1;
			}
			break;
		}
		default: {
			char vartype[2];

			putlog(LOG_MISC, "*", "converting unknown! '%c'", type);
			vartype[0] = type;
			vartype[1] = 0;
			JS_ReportError(cx, "Cannot convert JS object to unknown variable type '%s'", vartype);
			err = 1;
		}
	}

	return(err);
}

/* This function cleans up temporary buffers we used when calling
	a C command. */
static int my_argument_cleanup(mstack_t *args, mstack_t *bufs, mstack_t *cbacks, int err)
{
	int i;

	for (i = 0; i < bufs->len; i++) {
		free((void *)bufs->stack[i]);
	}
	if (err) {
		for (i = 0; i < cbacks->len; i++) {
			script_callback_t *cback;

			cback = (script_callback_t *)cbacks->stack[i];
			cback->del(cback);
		}
	}
	mstack_destroy(cbacks);
	mstack_destroy(bufs);
	mstack_destroy(args);
	return(0);
}

/* This handles all the JS commands we create. It converts the JS arguments
	to C variables, calls the C callback function, then converts
	the return value to a JS variable. */
static int my_command_handler(JSContext *cx, JSObject *obj, int argc, jsval *argv, jsval *rval)
{
	script_command_t *cmd;
	script_var_t retval, var;
	mstack_t *args, *bufs, *cbacks;
	void **al; /* Argument list to pass to the callback*/
	int nopts; /* Number of optional args we're passing. */
	int simple_retval; /* Return value for simple commands. */
	int i, err, skip;
	char *syntax;
	JSObject *this_obj;

	/* Start off with a void value. */
	*rval = JSVAL_VOID;

	/* I don't know how well documented this is, or if it's the best way
		to do it, but argv[-2] is the function's "this" object. */
	this_obj = JSVAL_TO_OBJECT(argv[-2]);

	/* Get the associated command structure stored in the object. */
	cmd = (script_command_t *)JS_GetPrivate(cx, this_obj);

	/* Check for proper number of args. */
	if (cmd->flags & SCRIPT_VAR_ARGS) err = !(cmd->nargs <= argc);
	else err = !(cmd->nargs < 0 || cmd->nargs == argc);

	if (err) {
		JS_ReportError(cx, "Wrong number of arguments: %s", cmd->syntax_error);
		return JS_FALSE;
	}

	/* We want space for at least 10 args. */
	args = mstack_new(2*argc+10);

	/* Reserve space for 3 optional args. */
	mstack_push(args, NULL);
	mstack_push(args, NULL);
	mstack_push(args, NULL);

	/* Have some space for buffers too. */
	bufs = mstack_new(argc);

	/* And callbacks. */
	cbacks = mstack_new(argc);

	/* Parse arguments. */
	syntax = cmd->syntax;
	if (cmd->flags & SCRIPT_VAR_FRONT) {
		/* See how many args to skip. */
		skip = strlen(syntax) - argc;
		if (skip < 0) skip = 0;
		for (i = 0; i < skip; i++) mstack_push(args, NULL);
		syntax += skip;
	}
	else skip = 0;

	err = 0;
	for (i = 0; i < argc; i++) {
		err = js_to_c_var(cx, obj, argv[i], &var, *syntax);

		if (err) break;

		if (var.type & SCRIPT_FREE) mstack_push(bufs, var.value);
		else if (*syntax == SCRIPT_CALLBACK) mstack_push(cbacks, var.value);

		mstack_push(args, var.value);
		syntax++;
	}

	if (err) {
		putlog(LOG_MISC, "*", "arg error");
		my_argument_cleanup(args, bufs, cbacks, 1);
		JS_ReportError(cx, "Invalid arguments, should be: %s", cmd->syntax_error);
		return JS_FALSE;
	}

	memset(&retval, 0, sizeof(retval));

	al = args->stack; /* Argument list shortcut name. */
	nopts = 0;
	if (cmd->flags & SCRIPT_PASS_COUNT) {
		al[2-nopts] = (void *)(args->len - 3 - skip);
		nopts++;
	}
	if (cmd->flags & SCRIPT_PASS_RETVAL) {
		al[2-nopts] = &retval;
		nopts++;
	}
	if (cmd->flags & SCRIPT_PASS_CDATA) {
		al[2-nopts] = cmd->client_data;
		nopts++;
	}
	al += (3-nopts);
	args->len -= (3-nopts);

	if (cmd->flags & SCRIPT_PASS_ARRAY) {
		simple_retval = cmd->callback(args->len, al);
	}
	else {
		simple_retval = cmd->callback(al[0], al[1], al[2], al[3], al[4]);
	}

	if (!(cmd->flags & SCRIPT_PASS_RETVAL)) {
		retval.type = cmd->retval_type;
		retval.len = -1;
		retval.value = (void *)simple_retval;
	}

	err = retval.type & SCRIPT_ERROR;
	c_to_js_var(cx, &retval, rval);

	my_argument_cleanup(args, bufs, cbacks, 0);

	if (err) return JS_FALSE;
	return JS_TRUE;
}


static int javascript_init()
{
	/* Create a new runtime environment. */
	global_js_runtime = JS_NewRuntime(0x100000);
	if (!global_js_runtime) return(1);

	/* Create a new context. */
	global_js_context = JS_NewContext(global_js_runtime, 0x1000);
	if (!global_js_context) return(1);

	/* Create the global object here */
	global_js_object = JS_NewObject(global_js_context, &global_class, NULL, NULL);

	/* Initialize the built-in JS objects and the global object */
	JS_InitStandardClasses(global_js_context, global_js_object);

	/* Now initialize our eggvar class. */
	JS_InitClass(global_js_context, global_js_object,
		NULL, &eggvar_class, NULL, 0,
		NULL, NULL, NULL, NULL);

	/* And eggfunc as well. */
	JS_InitClass(global_js_context, global_js_object,
		NULL, &eggfunc_class, NULL, 0,
		NULL, NULL, NULL, NULL);

	return(0);
}

static registry_simple_chain_t my_functions[] = {
	{"script", NULL, 0}, /* First arg gives our class */
	{"load script", my_load_script, 2}, /* name, ptr, nargs */
	{"link var", my_link_var, 2},
	{"unlink var", my_unlink_var, 2},
	{"create cmd", my_create_cmd, 2},
	{"delete cmd", my_delete_cmd, 2},
	{0}
};

static Function journal_table[] = {
	(Function)1,	/* Version */
	(Function)5,	/* Number of functions */
	my_load_script,
	my_link_var,
	my_unlink_var,
	my_create_cmd,
	my_delete_cmd
};

static Function journal_playback;
static void *journal_playback_h;

/* Here we process the dcc console .tcl command. */
static int cmd_js(struct userrec *u, int idx, char *text)
{
	static int curline = 1;
	int retval;
	jsval js_rval;

	if (!isowner(dcc[idx].nick)) {
		dprintf(idx, _("You must be a permanent owner (defined in the config file) to use this command.\n"));
		return(BIND_RET_LOG);
	}

	retval = JS_EvaluateScript(global_js_context, global_js_object,
			text, strlen(text), "console", curline++, &js_rval);
	if (!retval) {
		dprintf(idx, "JS Error: unknown for now\n");
	}
	else {
		JSString *str;

		str = JS_ValueToString(global_js_context, js_rval);
		if (!str) {
			dprintf(idx, "JS:\n");
		}
		else {
			dprintf(idx, "JS: %s\n", JS_GetStringBytes(str));
		}
	}
	return(0);
}

static void javascript_report(int idx, int details)
{
	JSVersion version;
	const char *version_str;

	version = JS_GetVersion(global_js_context);
	version_str = JS_VersionToString(version);

	if (!details) {
		dprintf(idx, "Using JavaScript version %s\n", version_str);
		return;
	}

	dprintf(idx, "    Using JavaScript version %s\n", version_str);
}

static cmd_t dcc_commands[] = {
	{"js", 	"n", 	(Function) cmd_js,	NULL},
	{0}
};

EXPORT_SCOPE char *javascript_LTX_start();
static char *javascript_close();

static Function javascript_table[] = {
	(Function) javascript_LTX_start,
	(Function) javascript_close,
	(Function) 0,
	(Function) javascript_report
};

char *javascript_LTX_start(eggdrop_t *eggdrop)
{
	bind_table_t *dcc_table;

	egg = eggdrop;

	javascript_init();

	module_register("javascript", javascript_table, 1, 2);
	if (!module_depend("javascript", "eggdrop", 107, 0)) {
		module_undepend("javascript");
		return "This module requires eggdrop1.7.0 or later";
	}

	error_logfile = strdup("logs/javascript_errors.log");

	registry_add_simple_chains(my_functions);
	registry_lookup("script", "playback", &journal_playback, &journal_playback_h);
	if (journal_playback) journal_playback(journal_playback_h, journal_table);

	dcc_table = find_bind_table2("dcc");
	if (dcc_table) add_builtins2(dcc_table, dcc_commands);

	return(NULL);
}

static char *javascript_close()
{
	bind_table_t *dcc_table;

	/* When tcl is gone from the core, this will be uncommented. */
	/* Tcl_DeleteInterp(ginterp); */

	dcc_table = find_bind_table2("dcc");
	if (dcc_table) rem_builtins2(dcc_table, dcc_commands);

	module_undepend("javascript");
	return(NULL);
}