/*
 * Copyright (C) 2011-2012 Felix Fietkau <nbd@openwrt.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License version 2.1
 * as published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include "libubus.h"
#include "libubus-internal.h"

void __hidden ubus_process_invoke(struct ubus_context *ctx, struct ubus_msghdr *hdr)
{
	struct blob_attr **attrbuf;
	struct ubus_request_data req;
	struct ubus_object *obj;
	int method;
	int ret = 0;

	req.peer = hdr->peer;
	req.seq = hdr->seq;
	attrbuf = ubus_parse_msg(hdr->data);

	if (!attrbuf[UBUS_ATTR_OBJID])
		return;

	req.object = blob_get_u32(attrbuf[UBUS_ATTR_OBJID]);

	if (!attrbuf[UBUS_ATTR_METHOD]) {
		ret = UBUS_STATUS_INVALID_ARGUMENT;
		goto send;
	}

	obj = avl_find_element(&ctx->objects, &req.object, obj, avl);
	if (!obj) {
		ret = UBUS_STATUS_NOT_FOUND;
		goto send;
	}

	for (method = 0; method < obj->n_methods; method++)
		if (!obj->methods[method].name ||
		    !strcmp(obj->methods[method].name,
		            blob_data(attrbuf[UBUS_ATTR_METHOD])))
			goto found;

	/* not found */
	ret = UBUS_STATUS_METHOD_NOT_FOUND;
	goto send;

found:
	ret = obj->methods[method].handler(ctx, obj, &req,
					   blob_data(attrbuf[UBUS_ATTR_METHOD]),
					   attrbuf[UBUS_ATTR_DATA]);
	if (req.deferred)
		return;

send:
	ubus_complete_deferred_request(ctx, &req, ret);
}

static void ubus_add_object_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct ubus_object *obj = req->priv;
	struct blob_attr **attrbuf = ubus_parse_msg(msg);

	if (!attrbuf[UBUS_ATTR_OBJID])
		return;

	obj->id = blob_get_u32(attrbuf[UBUS_ATTR_OBJID]);

	if (attrbuf[UBUS_ATTR_OBJTYPE])
		obj->type->id = blob_get_u32(attrbuf[UBUS_ATTR_OBJTYPE]);

	obj->avl.key = &obj->id;
	avl_insert(&req->ctx->objects, &obj->avl);
}

static void ubus_push_method_data(const struct ubus_method *m)
{
	void *mtbl;
	int i;

	mtbl = blobmsg_open_table(&b, m->name);

	for (i = 0; i < m->n_policy; i++)
		blobmsg_add_u32(&b, m->policy[i].name, m->policy[i].type);

	blobmsg_close_table(&b, mtbl);
}

static bool ubus_push_object_type(const struct ubus_object_type *type)
{
	void *s;
	int i;

	s = blob_nest_start(&b, UBUS_ATTR_SIGNATURE);

	for (i = 0; i < type->n_methods; i++)
		ubus_push_method_data(&type->methods[i]);

	blob_nest_end(&b, s);

	return true;
}

int ubus_add_object(struct ubus_context *ctx, struct ubus_object *obj)
{
	struct ubus_request req;
	int ret;

	blob_buf_init(&b, 0);

	if (obj->name && obj->type) {
		blob_put_string(&b, UBUS_ATTR_OBJPATH, obj->name);

		if (obj->type->id)
			blob_put_int32(&b, UBUS_ATTR_OBJTYPE, obj->type->id);
		else if (!ubus_push_object_type(obj->type))
			return UBUS_STATUS_INVALID_ARGUMENT;
	}

	if (ubus_start_request(ctx, &req, b.head, UBUS_MSG_ADD_OBJECT, 0) < 0)
		return UBUS_STATUS_INVALID_ARGUMENT;

	req.raw_data_cb = ubus_add_object_cb;
	req.priv = obj;
	ret = ubus_complete_request(ctx, &req, 0);
	if (ret)
		return ret;

	if (!obj->id)
		return UBUS_STATUS_NO_DATA;

	return 0;
}

static void ubus_remove_object_cb(struct ubus_request *req, int type, struct blob_attr *msg)
{
	struct ubus_object *obj = req->priv;
	struct blob_attr **attrbuf = ubus_parse_msg(msg);

	if (!attrbuf[UBUS_ATTR_OBJID])
		return;

	obj->id = 0;

	if (attrbuf[UBUS_ATTR_OBJTYPE] && obj->type)
		obj->type->id = 0;

	avl_delete(&req->ctx->objects, &obj->avl);
}

int ubus_remove_object(struct ubus_context *ctx, struct ubus_object *obj)
{
	struct ubus_request req;
	int ret;

	blob_buf_init(&b, 0);
	blob_put_int32(&b, UBUS_ATTR_OBJID, obj->id);

	if (ubus_start_request(ctx, &req, b.head, UBUS_MSG_REMOVE_OBJECT, 0) < 0)
		return UBUS_STATUS_INVALID_ARGUMENT;

	req.raw_data_cb = ubus_remove_object_cb;
	req.priv = obj;
	ret = ubus_complete_request(ctx, &req, 0);
	if (ret)
		return ret;

	if (obj->id)
		return UBUS_STATUS_NO_DATA;

	return 0;
}
