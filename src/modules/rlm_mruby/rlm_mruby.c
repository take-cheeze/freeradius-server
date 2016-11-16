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
 * @file rlm_example.c
 * @brief Example module code.
 *
 * @copyright 2013 The FreeRADIUS server project
 * @copyright 2013 your name \<your address\>
 */
RCSID("$Id$")

#include <freeradius-devel/radiusd.h>
#include <freeradius-devel/modules.h>
#include <freeradius-devel/rad_assert.h>

#include <mruby.h>
#include <mruby/compile.h>
#include <mruby/array.h>

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
} rlm_mruby_t;

/*
 *	A mapping of configuration file names to internal variables.
 */
static const CONF_PARSER module_config[] = {
	{ FR_CONF_OFFSET("filename", PW_TYPE_FILE_INPUT | PW_TYPE_REQUIRED, struct rlm_mruby_t, filename) },
	{ FR_CONF_OFFSET("module", PW_TYPE_STRING, struct rlm_mruby_t, module_name), .dflt = "Radiusd" },
	CONF_PARSER_TERMINATOR
};

/*
 *	Do any per-module initialization that is separate to each
 *	configured instance of the module.  e.g. set up connections
 *	to external databases, read configuration files, set up
 *	dictionary entries, etc.
 */
static int mod_instantiate(UNUSED CONF_SECTION *conf, void *instance)
{
	rlm_mruby_t *inst = instance;

	inst->mrb = mrb_open();
	if (!inst->mrb) {
		ERROR("mruby initialization failed");
		return -1;
	}

	return 0;
}

/*
 *	Find the named user in this modules database.  Create the set
 *	of attribute-value pairs to check and reply with for this user
 *	from the database. The authentication code only needs to check
 *	the password, the rest is done here.
 */
static rlm_rcode_t CC_HINT(nonnull) mod_authorize(UNUSED void *instance, UNUSED void *thread, REQUEST *request)
{
	VALUE_PAIR *state;

	/*
	 *  Look for the 'state' attribute.
	 */
	state = fr_pair_find_by_num(request->packet->vps, 0, PW_STATE, TAG_ANY);
	if (state != NULL) {
		RDEBUG("Found reply to access challenge");
		return RLM_MODULE_OK;
	}

	/*
	 *  Create the challenge, and add it to the reply.
	 */
	pair_make_reply("Reply-Message", "This is a challenge", T_OP_EQ);
	pair_make_reply("State", "0", T_OP_EQ);

	/*
	 *  Mark the packet as an Access-Challenge packet.
	 *
	 *  The server will take care of sending it to the user.
	 */
	request->reply->code = PW_CODE_ACCESS_CHALLENGE;
	RDEBUG("Sending Access-Challenge");

	return RLM_MODULE_HANDLED;
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

/*
 *	See if a user is already logged in. Sets request->simul_count to the
 *	current session count for this user and sets request->simul_mpp to 2
 *	if it looks like a multilink attempt based on the requested IP
 *	address, otherwise leaves request->simul_mpp alone.
 *
 *	Check twice. If on the first pass the user exceeds his
 *	max. number of logins, do a second pass and validate all
 *	logins by querying the terminal server (using eg. SNMP).
 */
static rlm_rcode_t CC_HINT(nonnull) mod_checksimul(UNUSED void *instance, UNUSED void *thread, REQUEST *request)
{
	request->simul_count = 0;

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
extern rad_module_t rlm_example;
rad_module_t rlm_example = {
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
		[MOD_SESSION]		= mod_checksimul
#endif
	},
};
