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
 * @copyright 2016 Herwin Weststrate <freeradius@herwinw.nl>
 * @copyright 2016 The FreeRADIUS server project
 */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include "rlm_mruby.h"

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

static mrb_value mruby_radlog(mrb_state *mrb, UNUSED mrb_value self)
{
	mrb_int level;
	char *msg = NULL;

	mrb_get_args(mrb, "iz", &level, &msg);
	radlog(&default_log, level, "rlm_ruby: %s", msg);

	return mrb_nil_value();
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
	mrb_state *mrb;
	struct RClass *mruby_module;
	FILE *f;
	mrb_value status;

	mrb = inst->mrb = mrb_open();
	if (!mrb) {
		ERROR("mruby initialization failed");
		return -1;
	}

	/* Define the freeradius module */
	DEBUG("Creating module %s", inst->module_name);
	mruby_module = mrb_define_module(mrb, inst->module_name);
	if (!mruby_module) {
		ERROR("Creating module %s failed", inst->module_name);
		return -1;
	}

	/* Define the radlog method */
	mrb_define_class_method(mrb, mruby_module, "radlog", mruby_radlog, MRB_ARGS_REQ(2));

#define A(x) mrb_define_const(mrb, mruby_module, #x, mrb_fixnum_value(x));
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
	inst->mruby_request = mruby_request_class(mrb, mruby_module);

	DEBUG("Loading file %s...", inst->filename);
	f = fopen(inst->filename, "r");
	if (!f) {
		ERROR("Opening file failed");
		return -1;
	}

	status = mrb_load_file(mrb, f);
	if (mrb_undef_p(status)) {
		ERROR("Parsing file failed");
		return -1;
	}
	fclose(f);

	status = mrb_funcall(mrb, mrb_top_self(mrb), "instantiate", 0);
	if (mrb_undef_p(status)) {
		ERROR("Running instantiate failed");
		return -1;
	}

	return 0;
}

#define BUF_SIZE 1024
static mrb_value mruby_vps_to_ary(mrb_state *mrb, VALUE_PAIR **vps)
{
	mrb_value res;
	VALUE_PAIR *vp;
	vp_cursor_t cursor;
	char buf[BUF_SIZE]; /* same size as fr_pair_fprint buffer */

	res = mrb_ary_new(mrb);
	for (vp = fr_cursor_init(&cursor, vps); vp; vp = fr_cursor_next(&cursor)) {
		mrb_value tmp, key, val;
		tmp = mrb_ary_new_capa(mrb, 2);
		if (vp->da->flags.has_tag) {
			snprintf(buf, BUF_SIZE, "%s:%d", vp->da->name, vp->tag);
			key = mrb_str_new_cstr(mrb, buf);
		} else {
			key = mrb_str_new_cstr(mrb, vp->da->name);
		}
		fr_pair_value_snprint(buf, sizeof(buf), vp, '\0');
		val = mrb_str_new_cstr(mrb, buf);
		mrb_ary_push(mrb, tmp, key);
		mrb_ary_push(mrb, tmp, val);

		mrb_ary_push(mrb, res, tmp);
	}

	return res;
}

static void add_vp_tuple(TALLOC_CTX *ctx, REQUEST *request, VALUE_PAIR **vps, mrb_state *mrb, mrb_value value, char const *function_name)
{
	int i;

	for (i = 0; i < RARRAY_LEN(value); i++) {
		mrb_value tuple = mrb_ary_entry(value, i);
		mrb_value key, val;
		char const *ckey, *cval;
		VALUE_PAIR *vp;
		vp_tmpl_t dst;
		FR_TOKEN op = T_OP_EQ;

		/* This tuple should be an array of length 2 */
		if (mrb_type(tuple) != MRB_TT_ARRAY) {
			REDEBUG("add_vp_tuple, %s: non-array passed at index %i", function_name, i);
			continue;
		}

		if (RARRAY_LEN(tuple) != 2 && RARRAY_LEN(tuple) != 3) {
			REDEBUG("add_vp_tuple, %s: array with incorrect length passed at index %i, expected 2 or 3, got %i", function_name, i, RARRAY_LEN(tuple));
			continue;
		}

		key = mrb_ary_entry(tuple, 0);
		val = mrb_ary_entry(tuple, -1);
		if (mrb_type(key) != MRB_TT_STRING) {
			REDEBUG("add_vp_tuple, %s: tuple element %i must have a string as first element", function_name, i);
			continue;
		}

		ckey = mrb_str_to_cstr(mrb, key);
		cval = mrb_str_to_cstr(mrb, mrb_obj_as_string(mrb, val));
		if (ckey == NULL || cval == NULL) {
			REDEBUG("%s: string conv failed", function_name);
			continue;
		}


		if (RARRAY_LEN(tuple) == 3) {
			if (mrb_type(mrb_ary_entry(tuple, 1)) != MRB_TT_STRING) {
				REDEBUG("Invalid type for operator, expected string, falling back to =");
			} else {
				char const *cop = mrb_str_to_cstr(mrb, mrb_ary_entry(tuple, 1));
				if (!(op = fr_str2int(fr_tokens_table, cop, 0))) {
					REDEBUG("Invalid operator: %s, falling back to =", cop);
					op = T_OP_EQ;
				}
			}
		}
		DEBUG("%s: %s %s %s", function_name, ckey, fr_int2str(fr_tokens_table, op, "="), cval);

		memset(&dst, 0, sizeof(dst));
		if (tmpl_from_attr_str(&dst, ckey, REQUEST_CURRENT, PAIR_LIST_REPLY, false, false) <= 0) {
			ERROR("Failed to find attribute %s", ckey);
			continue;
		}

		if (radius_request(&request, dst.tmpl_request) < 0) {
			ERROR("Attribute name %s refers to outer request but not in a tunnel, skipping...", ckey);
			continue;
		}

		if (!(vp = fr_pair_afrom_da(ctx, dst.tmpl_da))) {
			ERROR("Failed to create attribute %s", ckey);
			continue;
		}

		vp->op = op;
		if (fr_pair_value_from_str(vp, cval, -1) < 0) {
			REDEBUG("%s: %s %s %s failed", function_name, ckey, fr_int2str(fr_tokens_table, op, "="), cval);
		} else {
			DEBUG("%s: %s %s %s OK", function_name, ckey, fr_int2str(fr_tokens_table, op, "="), cval);
		}

		radius_pairmove(request, vps, vp, false);
	}
}

static void mruby_set_vps(mrb_state *mrb, mrb_value mruby_request, char const *name, VALUE_PAIR **vps)
{
	mrb_iv_set(mrb, mruby_request, mrb_intern_cstr(mrb, name), mruby_vps_to_ary(mrb, vps));
}

static rlm_rcode_t CC_HINT(nonnull) do_mruby(REQUEST *request, rlm_mruby_t const *inst, char const *function_name)
{
	mrb_state *mrb = inst->mrb;
	mrb_value mruby_request, mruby_result;

	mruby_request = mrb_obj_new(mrb, inst->mruby_request, 0, NULL);
	mruby_set_vps(mrb, mruby_request, "@request", &request->packet->vps);
	mruby_set_vps(mrb, mruby_request, "@reply", &request->reply->vps);
	mruby_set_vps(mrb, mruby_request, "@control", &request->control);
	mruby_set_vps(mrb, mruby_request, "@session_state", &request->state);
#ifdef WITH_PROXY
	if (request->proxy) {
		mruby_set_vps(mrb, mruby_request, "@proxy_request", &request->proxy->packet->vps);
		mruby_set_vps(mrb, mruby_request, "@proxy_reply", &request->proxy->reply->vps);
	}
#endif
	mruby_result = mrb_funcall(mrb, mrb_top_self(mrb), function_name, 1, mruby_request);

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
			return (rlm_rcode_t)mrb_int(mrb, mruby_result);
			break;
		case MRB_TT_ARRAY:
			/* Must have exactly three items */
			if (RARRAY_LEN(mruby_result) != 3) {
				ERROR("Expected array to have exactly three values, got %i instead", RARRAY_LEN(mruby_result));
				return RLM_MODULE_FAIL;
			}

			/* First item must be a Fixnum, this will be the return type */
			if (mrb_type(mrb_ary_entry(mruby_result, 0)) != MRB_TT_FIXNUM) {
				ERROR("Expected first array element to be a Fixnum, got %s instead", RSTRING_PTR(mrb_obj_as_string(mrb, mrb_ary_entry(mruby_result, 0))));
				return RLM_MODULE_FAIL;
			}

			/* Second and third items must be Arrays, these will be the updates for reply and control */
			if (mrb_type(mrb_ary_entry(mruby_result, 1)) != MRB_TT_ARRAY) {
				ERROR("Expected second array element to be an Array, got %s instead", RSTRING_PTR(mrb_obj_as_string(mrb, mrb_ary_entry(mruby_result, 1))));
				return  RLM_MODULE_FAIL;
			} else if (mrb_type(mrb_ary_entry(mruby_result, 2)) != MRB_TT_ARRAY) {
				ERROR("Expected third array element to be an Array, got %s instead", RSTRING_PTR(mrb_obj_as_string(mrb, mrb_ary_entry(mruby_result, 2))));
				return RLM_MODULE_FAIL;
			}

			add_vp_tuple(request->reply, request, &request->reply->vps, mrb, mrb_ary_entry(mruby_result, 1), function_name);
			add_vp_tuple(request, request, &request->control, mrb, mrb_ary_entry(mruby_result, 2), function_name);
			return (rlm_rcode_t)mrb_int(mrb, mrb_ary_entry(mruby_result, 0));
			break;
		default:
			/* Invalid return type */
			ERROR("Expected return to be a Fixnum or an Array, got %s instead", RSTRING_PTR(mrb_obj_as_string(mrb, mruby_result)));
			return RLM_MODULE_FAIL;
			break;
	}
}


#define RLM_MRUBY_FUNC(foo) static rlm_rcode_t CC_HINT(nonnull) mod_##foo(void *instance, UNUSED void *thread, REQUEST *request) \
	{ \
		return do_mruby(request,	\
			       (rlm_mruby_t const *)instance, \
			       #foo); \
	}

RLM_MRUBY_FUNC(authorize)
RLM_MRUBY_FUNC(authenticate)
RLM_MRUBY_FUNC(post_auth)
#ifdef WITH_ACCOUNTING
RLM_MRUBY_FUNC(preacct)
RLM_MRUBY_FUNC(accounting)
RLM_MRUBY_FUNC(session)
#endif
#ifdef WITH_PROXY
RLM_MRUBY_FUNC(pre_proxy)
RLM_MRUBY_FUNC(post_proxy)
#endif
#ifdef WITH_COA
RLM_MRUBY_FUNC(recv_coa)
RLM_MRUBY_FUNC(send_coa)
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
		[MOD_POST_AUTH]		= mod_post_auth,
#ifdef WITH_ACCOUNTING
		[MOD_PREACCT]		= mod_preacct,
		[MOD_ACCOUNTING]	= mod_accounting,
		[MOD_SESSION]		= mod_session,
#endif
#ifdef WITH_PROXY
		[MOD_PRE_PROXY]		= mod_pre_proxy,
		[MOD_POST_PROXY]	= mod_post_proxy,
#endif
#ifdef WITH_COA
		[MOD_RECV_COA]		= mod_recv_coa,
		[MOD_SEND_COA]		= mod_send_coa,
#endif
	},
};
