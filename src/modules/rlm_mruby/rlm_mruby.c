/*
 *   This program is is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; either version 2 of the License, or (at
 *   your option) any later version.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License
 *   along with this program; if not, write to the Free Software
 *   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

/**
 * $Id$
 * @file rlm_mruby.c
 * @brief Translates requests between the server an an mruby interpreter.
 *
 * @copyright 2016 The FreeRADIUS server project
 * @copyright 2016 Herwin Weststrate <herwinw@herwinw.nl>
 */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/array.h>
#include <mruby/numeric.h>
#include <mruby/string.h>
#include <mruby/variable.h>

/*
 *	Define a structure for our module configuration.
 *
 *	These variables do not need to be in a structure, but it's
 *	a lot cleaner to do so, and a pointer to the structure can
 *	be used as the instance handle.
 */
typedef struct rlm_mruby_t {
	char const *filename;
	char const *module_name;

	mrb_state *mrb;

	struct RClass *mruby_request;
} rlm_mruby_t;

/*
 *	A mapping of configuration file names to internal variables.
 */
static const CONF_PARSER module_config[] = {
	{ FR_CONF_OFFSET("filename", PW_TYPE_FILE_INPUT | PW_TYPE_REQUIRED, struct rlm_mruby_t, filename) },
	{ FR_CONF_OFFSET("module", PW_TYPE_STRING, struct rlm_mruby_t, module_name), .dflt = "Radiusd" },
	CONF_PARSER_TERMINATOR
};

static mrb_value mruby_radlog(mrb_state *mrb, UNUSED mrb_value self) {
	mrb_int level;
	char *msg = NULL;

	mrb_get_args(mrb, "iz", &level, &msg);
	radlog(&default_log, level, "rlm_ruby: %s", msg);

	return mrb_nil_value();
}

static mrb_value mruby_request_request(mrb_state *mrb, mrb_value self) {
	return mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "@request"));
}

/*
 *	Do any per-module initialization that is separate to each
 *	configured instance of the module.  e.g. set up connections
 *	to external databases, read configuration files, set up
 *	dictionary entries, etc.
 */
static int mod_instantiate(UNUSED CONF_SECTION *conf, void *instance)
{
	rlm_mruby_t *inst = instance;
	FILE *f;
	struct RClass *module;
	mrb_value status;

	inst->mrb = mrb_open();
	if (!inst->mrb) {
		ERROR("mruby initialization failed");
		return -1;
	}

	/* Define the freeradius module */
	DEBUG("Creating module %s", inst->module_name);
	module = mrb_define_module(inst->mrb, inst->module_name);
	if (!module) {
		ERROR("Creating module %s failed", inst->module_name);
		return -1;
	}

	/* Define the radlog method */
	mrb_define_class_method(inst->mrb, module, "radlog", mruby_radlog, MRB_ARGS_REQ(2));

#define A(x) mrb_define_const(inst->mrb, module, #x, mrb_fixnum_value(x));
	/* Define the logging constants */
	A(L_DBG);
	A(L_WARN);
	A(L_AUTH);
	A(L_INFO);
	A(L_ERR);
	A(L_PROXY);
	A(L_WARN);
	A(L_ACCT);
	A(L_DBG_WARN);
	A(L_DBG_ERR);
	A(L_DBG_WARN_REQ);
	A(L_DBG_ERR_REQ);

	/* Define the return value constants */
	A(RLM_MODULE_REJECT)
	A(RLM_MODULE_FAIL)
	A(RLM_MODULE_OK)
	A(RLM_MODULE_HANDLED)
	A(RLM_MODULE_INVALID)
	A(RLM_MODULE_USERLOCK)
	A(RLM_MODULE_NOTFOUND)
	A(RLM_MODULE_NOOP)
	A(RLM_MODULE_UPDATED)
	A(RLM_MODULE_NUMCODES)
#undef A

	/* Define the Request class */
	inst->mruby_request = mrb_define_class_under(inst->mrb, module, "Request", inst->mrb->object_class);
	if (!inst->mruby_request) {
		ERROR("Creating class %s:Request failed", inst->module_name);
		return -1;
	}

	/* Add lists to the Request class */
	/* FIXME: Use attr_reader (if available) */
	mrb_define_method(inst->mrb, inst->mruby_request, "request", mruby_request_request, MRB_ARGS_NONE());


	DEBUG("Loading file %s...", inst->filename);
	f = fopen(inst->filename, "r");
	if (!f) {
		ERROR("Opening file failed");
		return -1;
	}

	status = mrb_load_file(inst->mrb, f);
	if (mrb_undef_p(status)) {
		ERROR("Parsing file failed");
		return -1;
	}
	fclose(f);

	status = mrb_funcall(inst->mrb, mrb_top_self(inst->mrb), "instantiate", 0);
	if (mrb_undef_p(status)) {
		ERROR("Running instantiate failed");
		return -1;
	}

	return 0;
}

#define BUF_SIZE 1024
static mrb_value mruby_request_to_ary(rlm_mruby_t *inst, REQUEST *request)
{
	mrb_value res;
	VALUE_PAIR *vp;
	vp_cursor_t cursor;
	char buf[BUF_SIZE]; /* same size as fr_pair_fprint buffer */

	res = mrb_ary_new(inst->mrb);
	for (vp = fr_cursor_init(&cursor, &request->packet->vps); vp; vp = fr_cursor_next(&cursor)) {
		mrb_value tmp, key, val;
		tmp = mrb_ary_new_capa(inst->mrb, 2);
		if (vp->da->flags.has_tag) {
			snprintf(buf, BUF_SIZE, "%s:%d", vp->da->name, vp->tag);
			key = mrb_str_new_cstr(inst->mrb, buf);
		} else {
			key = mrb_str_new_cstr(inst->mrb, vp->da->name);
		}
		fr_pair_value_snprint(buf, sizeof(buf), vp, '\0');
		val = mrb_str_new_cstr(inst->mrb, buf);
		mrb_ary_push(inst->mrb, tmp, key);
		mrb_ary_push(inst->mrb, tmp, val);

		mrb_ary_push(inst->mrb, res, tmp);
	}

	return res;
}

static rlm_rcode_t CC_HINT(nonnull) mod_authorize(void *instance, UNUSED void *thread, REQUEST *request)
{
	rlm_mruby_t *inst = instance;
	mrb_value mruby_request, mruby_result;
	rlm_rcode_t rcode;

	mruby_request = mrb_obj_new(inst->mrb, inst->mruby_request, 0, NULL);
	mrb_iv_set(inst->mrb, mruby_request, mrb_intern_cstr(inst->mrb, "@request"), mruby_request_to_ary(inst, request));
	mruby_result = mrb_funcall(inst->mrb, mrb_top_self(inst->mrb), "authorize", 1, mruby_request);

	/* Two options for the return value:
	 * - a fixnum: convert to rlm_rcode_t, and return that
	 * - an array: this should have exactly three items in it. The first one
	 *             should be a fixnum, this will once again be converted to
	 *             rlm_rcode_t and eventually returned. The other two items
	 *             should be arrays. The items of the first array should be
	 *             merged into reply, the second array into control.
	 */
	switch (mrb_type(mruby_result)) {
		/* If it is a Fixnum: return that value */
		case MRB_TT_FIXNUM:
			rcode = (rlm_rcode_t)mrb_to_flo(inst->mrb, mruby_result);
			break;
		default:
			/* Invalid return type */
			ERROR("Expected return to be a Fixnum or an Array, got %s instead", RSTRING_PTR(mrb_obj_as_string(inst->mrb, mruby_result)));
			rcode = RLM_MODULE_FAIL;
			break;
	}


	return rcode;
}

/*
 *	Authenticate the user with the given password.
 */
static rlm_rcode_t CC_HINT(nonnull) mod_authenticate(UNUSED void *instance, UNUSED void *thread, UNUSED REQUEST *request)
{
	return RLM_MODULE_OK;
}

#ifdef WITH_ACCOUNTING
/*
 *	Massage the request before recording it or proxying it
 */
static rlm_rcode_t CC_HINT(nonnull) mod_preacct(UNUSED void *instance, UNUSED void *thread, UNUSED REQUEST *request)
{
	return RLM_MODULE_OK;
}

/*
 *	Write accounting information to this modules database.
 */
static rlm_rcode_t CC_HINT(nonnull) mod_accounting(UNUSED void *instance, UNUSED void *thread, UNUSED REQUEST *request)
{
	return RLM_MODULE_OK;
}

static rlm_rcode_t CC_HINT(nonnull) mod_session(UNUSED void *instance, UNUSED void *thread, UNUSED REQUEST *request)
{
	return RLM_MODULE_OK;
}
#endif


/*
 *	Only free memory we allocated.  The strings allocated via
 *	cf_section_parse() do not need to be freed.
 */
static int mod_detach(void *instance)
{
	rlm_mruby_t *inst = instance;

	mrb_close(inst->mrb);

	return 0;
}

/*
 *	The module name should be the only globally exported symbol.
 *	That is, everything else should be 'static'.
 *
 *	If the module needs to temporarily modify it's instantiation
 *	data, the type should be changed to RLM_TYPE_THREAD_UNSAFE.
 *	The server will then take care of ensuring that the module
 *	is single-threaded.
 */
extern rad_module_t rlm_mruby;
rad_module_t rlm_mruby = {
	.magic		= RLM_MODULE_INIT,
	.name		= "mruby",
	.type		= RLM_TYPE_THREAD_UNSAFE, /* Not sure */
	.inst_size	= sizeof(rlm_mruby_t),
	.config		= module_config,
	.instantiate	= mod_instantiate,
	.detach		= mod_detach,
	.methods = {
		[MOD_AUTHENTICATE]	= mod_authenticate,
		[MOD_AUTHORIZE]		= mod_authorize,
#ifdef WITH_ACCOUNTING
		[MOD_PREACCT]		= mod_preacct,
		[MOD_ACCOUNTING]	= mod_accounting,
		[MOD_SESSION]		= mod_session
#endif
	},
};
