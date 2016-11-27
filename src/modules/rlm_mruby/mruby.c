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

#include "rlm_mruby.h"

static mrb_value mruby_request_request(mrb_state *mrb, mrb_value self)
{
	return mrb_iv_get(mrb, self, mrb_intern_cstr(mrb, "@request"));
}

struct RClass *mruby_request_class(mrb_state *mrb, struct RClass *parent)
{
	struct RClass *request;

	request = mrb_define_class_under(mrb, parent, "Request", mrb->object_class);

	/* Add lists to the Request class */
	/* FIXME: Use attr_reader (if available) */
	mrb_define_method(mrb, request, "request", mruby_request_request, MRB_ARGS_NONE());

	return request;
}
