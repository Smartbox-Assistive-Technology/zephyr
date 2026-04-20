/*
 * Copyright (c) 2025 Smartbox Assistive Technology Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stddef.h>
#include <stdint.h>

#include <zephyr/kernel.h>
#include <zephyr/sys/__assert.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/byteorder.h>

#include <zephyr/usb/usbd.h>
#include <zephyr/usb/usb_ch9.h>
#include <zephyr/usb/class/usbd_uac1.h>
#include <zephyr/drivers/usb/udc.h>

#include "usbd_uac1_macros.h"

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(usbd_uac1, CONFIG_USBD_UAC1_LOG_LEVEL);

#define DT_DRV_COMPAT zephyr_uac1

/* Count endpoint buffers needed across all instances */
#define COUNT_UAC1_AS_EP_BUFFERS(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		+ 1 /* data endpoint */						\
		+ AS_IS_USB_ISO_OUT(node) /* OUT double buffering */		\
		+ AS_IS_USB_ISO_IN(node) /* IN double buffering */		\
		+ 2 * AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node)))
#define COUNT_UAC1_EP_BUFFERS(i)						\
	DT_INST_FOREACH_CHILD(i, COUNT_UAC1_AS_EP_BUFFERS)
#define UAC1_NUM_EP_BUFFERS DT_INST_FOREACH_STATUS_OKAY(COUNT_UAC1_EP_BUFFERS)

UDC_BUF_POOL_DEFINE(uac1_pool, UAC1_NUM_EP_BUFFERS, 6,
		     sizeof(struct udc_buf_info), NULL);

/* UAC1 Audio Class-Specific Request Codes (audio10 Table A-9) */
#define UAC1_SET_CUR	0x01
#define UAC1_GET_CUR	0x81
#define UAC1_SET_MIN	0x02
#define UAC1_GET_MIN	0x82
#define UAC1_SET_MAX	0x03
#define UAC1_GET_MAX	0x83
#define UAC1_SET_RES	0x04
#define UAC1_GET_RES	0x84

/* Endpoint control selectors */
#define EP_CONTROL_SAMPLING_FREQ	0x01

#define SET_CLASS_ITF_REQUEST		0x21
#define GET_CLASS_ITF_REQUEST		0xA1
#define SET_CLASS_EP_REQUEST		0x22
#define GET_CLASS_EP_REQUEST		0xA2

#define CONTROL_ENTITY_ID(setup)	((setup->wIndex & 0xFF00) >> 8)
#define CONTROL_SELECTOR(setup)		((setup->wValue & 0xFF00) >> 8)
#define CONTROL_CHANNEL(setup)		(setup->wValue & 0x00FF)

/* ---- Runtime data ---- */
struct uac1_ctx {
	const struct uac1_ops *ops;
	void *user_data;
	atomic_t as_active;
	atomic_t as_queued;
	atomic_t as_double;
	uint32_t fb_queued;
	uint32_t fb_double;
};

/* ---- Constant (ROM) data ---- */
struct uac1_cfg {
	struct usbd_class_data *const c_data;
	const struct usb_desc_header **fs_descriptors;
	const uac1_entity_type_t *entity_types;
	const uint16_t *ep_indexes;
	const uint16_t *fb_indexes;
	const uint8_t *as_terminals;
	uint8_t num_ifaces;
	uint8_t num_entities;
};

/* ---- Helper functions (mirrors UAC2 pattern) ---- */

static const struct usb_ep_descriptor *
get_as_data_ep(struct usbd_class_data *const c_data, int as_idx)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;

	if ((as_idx >= 0) && (as_idx < cfg->num_ifaces) &&
	    cfg->ep_indexes[as_idx] && cfg->fs_descriptors) {
		return (const struct usb_ep_descriptor *)
			cfg->fs_descriptors[cfg->ep_indexes[as_idx]];
	}
	return NULL;
}

static const struct usb_ep_descriptor *
get_as_feedback_ep(struct usbd_class_data *const c_data, int as_idx)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;

	if ((as_idx < cfg->num_ifaces) && cfg->fb_indexes[as_idx] &&
	    cfg->fs_descriptors) {
		return (const struct usb_ep_descriptor *)
			cfg->fs_descriptors[cfg->fb_indexes[as_idx]];
	}
	return NULL;
}

static int ep_to_as_interface(const struct device *dev, uint8_t ep, bool *fb)
{
	const struct uac1_cfg *cfg = dev->config;
	const struct usb_ep_descriptor *desc;

	for (int i = 0; i < cfg->num_ifaces; i++) {
		if (!cfg->ep_indexes[i]) {
			continue;
		}
		desc = get_as_data_ep(cfg->c_data, i);
		if (desc && (ep == desc->bEndpointAddress)) {
			*fb = false;
			return i;
		}
		desc = get_as_feedback_ep(cfg->c_data, i);
		if (desc && (ep == desc->bEndpointAddress)) {
			*fb = true;
			return i;
		}
	}
	*fb = false;
	return -ENOENT;
}

static int terminal_to_as_interface(const struct device *dev, uint8_t terminal)
{
	const struct uac1_cfg *cfg = dev->config;

	for (int i = 0; i < cfg->num_ifaces; i++) {
		if (terminal == cfg->as_terminals[i]) {
			return i;
		}
	}
	return -ENOENT;
}

/* ---- Ops registration ---- */

void usbd_uac1_set_ops(const struct device *dev,
		       const struct uac1_ops *ops, void *user_data)
{
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;

	__ASSERT(ops->sof_cb, "SOF callback is mandatory");
	__ASSERT(ops->terminal_update_cb, "terminal_update_cb is mandatory");

	for (uint8_t i = 0U; i < cfg->num_ifaces; i++) {
		if (cfg->ep_indexes[i]) {
			const struct usb_ep_descriptor *desc =
				get_as_data_ep(cfg->c_data, i);
			if (desc && USB_EP_DIR_IS_OUT(desc->bEndpointAddress)) {
				__ASSERT(ops->get_recv_buf, "get_recv_buf mandatory");
				__ASSERT(ops->data_recv_cb, "data_recv_cb mandatory");
			}
			if (desc && USB_EP_DIR_IS_IN(desc->bEndpointAddress)) {
				__ASSERT(ops->buf_release_cb, "buf_release_cb mandatory");
			}
		}
		if (cfg->fb_indexes[i]) {
			__ASSERT(ops->feedback_cb, "feedback_cb mandatory");
		}
	}

	ctx->ops = ops;
	ctx->user_data = user_data;
}

/* ---- Buffer allocation ---- */

static struct net_buf *
uac1_buf_alloc(const uint8_t ep, void *data, uint16_t size)
{
	struct net_buf *buf = NULL;
	struct udc_buf_info *bi;

	__ASSERT(IS_UDC_ALIGNED(data), "Unaligned buffer from application");

	buf = net_buf_alloc_with_data(&uac1_pool, data, size, K_NO_WAIT);
	if (!buf) {
		return NULL;
	}

	bi = udc_get_buf_info(buf);
	bi->ep = ep;

	if (USB_EP_DIR_IS_OUT(ep)) {
		buf->len = 0;
	}

	return buf;
}

/* ---- Data send (for IN terminals) ---- */

int usbd_uac1_send(const struct device *dev, uint8_t terminal,
		    void *data, uint16_t size)
{
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;
	struct net_buf *buf;
	const struct usb_ep_descriptor *desc;
	atomic_t *queued_bits = &ctx->as_queued;
	uint8_t ep = 0;
	int as_idx = terminal_to_as_interface(dev, terminal);
	int ret;

	desc = get_as_data_ep(cfg->c_data, as_idx);
	if (desc) {
		ep = desc->bEndpointAddress;
	}

	if (!ep) {
		return -ENOENT;
	}

	if (!atomic_test_bit(&ctx->as_active, as_idx)) {
		ctx->ops->buf_release_cb(dev, terminal, data, ctx->user_data);
		return 0;
	}

	if (atomic_test_and_set_bit(queued_bits, as_idx)) {
		queued_bits = &ctx->as_double;
		if (atomic_test_and_set_bit(queued_bits, as_idx)) {
			return -EAGAIN;
		}
	}

	buf = uac1_buf_alloc(ep, data, size);
	if (!buf) {
		atomic_clear_bit(queued_bits, as_idx);
		return -ENOMEM;
	}

	ret = usbd_ep_enqueue(cfg->c_data, buf);
	if (ret) {
		net_buf_unref(buf);
		atomic_clear_bit(queued_bits, as_idx);
	}

	return ret;
}

/* ---- ISO OUT read scheduling ---- */

static void schedule_iso_out_read(struct usbd_class_data *const c_data,
				  uint8_t ep, uint16_t mps, uint8_t terminal)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;
	struct net_buf *buf;
	atomic_t *queued_bits = &ctx->as_queued;
	void *data_buf;
	int as_idx = terminal_to_as_interface(dev, terminal);
	int ret;

	__ASSERT_NO_MSG((as_idx >= 0) && (as_idx < cfg->num_ifaces));
	ARG_UNUSED(cfg);

	if (!((as_idx >= 0) && atomic_test_bit(&ctx->as_active, as_idx))) {
		return;
	}

	if (atomic_test_and_set_bit(queued_bits, as_idx)) {
		queued_bits = &ctx->as_double;
		if (atomic_test_and_set_bit(queued_bits, as_idx)) {
			return;
		}
	}

	data_buf = ctx->ops->get_recv_buf(dev, terminal, mps, ctx->user_data);
	if (!data_buf) {
		LOG_ERR_RATELIMIT("No data buffer for terminal %d", terminal);
		atomic_clear_bit(queued_bits, as_idx);
		return;
	}

	buf = uac1_buf_alloc(ep, data_buf, mps);
	if (!buf) {
		ctx->ops->data_recv_cb(dev, terminal, data_buf, 0,
				       ctx->user_data);
		atomic_clear_bit(queued_bits, as_idx);
		return;
	}

	ret = usbd_ep_enqueue(c_data, buf);
	if (ret) {
		net_buf_unref(buf);
		ctx->ops->data_recv_cb(dev, terminal, data_buf, 0,
				       ctx->user_data);
		atomic_clear_bit(queued_bits, as_idx);
	}
}

/* ---- Explicit feedback write ---- */

static void write_explicit_feedback(struct usbd_class_data *const c_data,
				    uint8_t ep, uint8_t terminal)
{
	const struct device *dev = usbd_class_get_private(c_data);
	struct uac1_ctx *ctx = dev->data;
	struct net_buf *buf;
	struct udc_buf_info *bi;
	uint32_t fb_value;
	int as_idx = terminal_to_as_interface(dev, terminal);
	int ret;

	__ASSERT_NO_MSG(as_idx >= 0);

	buf = net_buf_alloc(&uac1_pool, K_NO_WAIT);
	if (!buf) {
		LOG_ERR("No buf for feedback");
		return;
	}

	bi = udc_get_buf_info(buf);
	bi->ep = ep;

	fb_value = ctx->ops->feedback_cb(dev, terminal, ctx->user_data);

	/* UAC1 Full-Speed: always Q10.14 in 3 bytes */
	net_buf_add_le24(buf, fb_value);

	ret = usbd_ep_enqueue(c_data, buf);
	if (ret) {
		net_buf_unref(buf);
	} else {
		if (ctx->fb_queued & BIT(as_idx)) {
			ctx->fb_double |= BIT(as_idx);
		} else {
			ctx->fb_queued |= BIT(as_idx);
		}
	}
}

/* ---- Alternate setting update ---- */

static void uac1_update(struct usbd_class_data *const c_data,
			 uint8_t iface, uint8_t alternate)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;
	const struct usb_desc_header **descriptors = cfg->fs_descriptors;
	const struct usb_association_descriptor *iad;
	const struct usb_ep_descriptor *data_ep, *fb_ep;
	uint8_t as_idx;

	LOG_DBG("iface %d alt %d", iface, alternate);

	if (!descriptors) {
		return;
	}

	iad = (const struct usb_association_descriptor *)descriptors[0];

	__ASSERT_NO_MSG((iface > iad->bFirstInterface) &&
			(iface < iad->bFirstInterface + iad->bInterfaceCount));
	as_idx = iface - iad->bFirstInterface - 1;

	/* UAC1 is Full-Speed only, so microframes is always false */
	ctx->ops->terminal_update_cb(dev, cfg->as_terminals[as_idx], alternate,
				     false, ctx->user_data);

	if (alternate == 0) {
		atomic_clear_bit(&ctx->as_active, as_idx);
		return;
	}

	atomic_set_bit(&ctx->as_active, as_idx);

	data_ep = get_as_data_ep(c_data, as_idx);
	__ASSERT_NO_MSG(data_ep);

	if (USB_EP_DIR_IS_OUT(data_ep->bEndpointAddress)) {
		schedule_iso_out_read(c_data, data_ep->bEndpointAddress,
				      sys_le16_to_cpu(data_ep->wMaxPacketSize),
				      cfg->as_terminals[as_idx]);

		fb_ep = get_as_feedback_ep(c_data, as_idx);
		if (fb_ep) {
			write_explicit_feedback(c_data, fb_ep->bEndpointAddress,
						cfg->as_terminals[as_idx]);
		}
	}
}

/* ---- Control request handling ---- */

/* UAC1 endpoint-directed sample rate control */
static int uac1_control_to_host(struct usbd_class_data *const c_data,
				const struct usb_setup_packet *const setup,
				struct net_buf *const buf)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;

	ARG_UNUSED(cfg);

	if (setup->bmRequestType == GET_CLASS_EP_REQUEST) {
		uint8_t cs = CONTROL_SELECTOR(setup);

		if (cs == EP_CONTROL_SAMPLING_FREQ) {
			/* UAC1 sample rate is 3 bytes LE.
			 * We only support 48000 Hz (from DTS).
			 * Respond to GET_CUR, GET_MIN, GET_MAX with 48000,
			 * and GET_RES with 0 (single frequency).
			 */
			uint32_t freq;

			switch (setup->bRequest) {
			case UAC1_GET_CUR:
			case UAC1_GET_MIN:
			case UAC1_GET_MAX:
				freq = 48000;
				break;
			case UAC1_GET_RES:
				freq = 0;
				break;
			default:
				errno = -ENOTSUP;
				return 0;
			}

			LOG_DBG("EP sample rate GET 0x%02x -> %u",
				setup->bRequest, freq);
			net_buf_add_u8(buf, freq & 0xFF);
			net_buf_add_u8(buf, (freq >> 8) & 0xFF);
			net_buf_add_u8(buf, (freq >> 16) & 0xFF);
			return 0;
		}
	}

	errno = -ENOTSUP;
	return 0;
}

static int uac1_control_to_dev(struct usbd_class_data *const c_data,
			       const struct usb_setup_packet *const setup,
			       const struct net_buf *const buf)
{
	/* UAC1 SET requests to endpoints for sample rate */
	if (setup->bmRequestType == SET_CLASS_EP_REQUEST) {
		uint8_t cs = CONTROL_SELECTOR(setup);

		if (cs == EP_CONTROL_SAMPLING_FREQ &&
		    setup->bRequest == UAC1_SET_CUR) {
			LOG_DBG("EP sample rate SET_CUR (ignoring - fixed rate)");
			/* Accept but ignore - we only support what DTS declares */
			return 0;
		}
	}

	errno = -ENOTSUP;
	return 0;
}

/* ---- Transfer completion ---- */

static int uac1_request(struct usbd_class_data *const c_data,
			struct net_buf *buf, int err)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;
	struct usbd_context *uds_ctx = usbd_class_get_ctx(c_data);
	struct udc_buf_info *bi;
	uint8_t ep, terminal;
	uint16_t mps;
	int as_idx;
	bool is_feedback;

	bi = udc_get_buf_info(buf);
	if (err) {
		if (err == -ECONNABORTED) {
			LOG_WRN("request ep 0x%02x cancelled", bi->ep);
		} else {
			LOG_ERR("request ep 0x%02x failed", bi->ep);
		}
	}

	mps = buf->size;
	ep = bi->ep;
	as_idx = ep_to_as_interface(dev, ep, &is_feedback);
	__ASSERT_NO_MSG((as_idx >= 0) && (as_idx < cfg->num_ifaces));
	terminal = cfg->as_terminals[as_idx];

	if (is_feedback) {
		bool clear_double = buf->frags;

		if (ctx->fb_queued & BIT(as_idx)) {
			ctx->fb_queued &= ~BIT(as_idx);
		} else {
			clear_double = true;
		}
		if (clear_double) {
			ctx->fb_double &= ~BIT(as_idx);
		}
	} else if (!atomic_test_and_clear_bit(&ctx->as_queued, as_idx) ||
		   buf->frags) {
		atomic_clear_bit(&ctx->as_double, as_idx);
	}

	if (USB_EP_DIR_IS_OUT(ep)) {
		ctx->ops->data_recv_cb(dev, terminal, buf->__buf, buf->len,
				       ctx->user_data);
		if (buf->frags) {
			ctx->ops->data_recv_cb(dev, terminal,
					       buf->frags->__buf,
					       buf->frags->len,
					       ctx->user_data);
		}
	} else if (!is_feedback) {
		ctx->ops->buf_release_cb(dev, terminal, buf->__buf,
					 ctx->user_data);
		if (buf->frags) {
			ctx->ops->buf_release_cb(dev, terminal,
						 buf->frags->__buf,
						 ctx->user_data);
		}
	}

	usbd_ep_buf_free(uds_ctx, buf);
	if (err) {
		return 0;
	}

	if (USB_EP_DIR_IS_OUT(ep)) {
		schedule_iso_out_read(c_data, ep, mps, terminal);
	} else if (is_feedback) {
		write_explicit_feedback(c_data, ep, cfg->as_terminals[as_idx]);
	}

	return 0;
}

/* ---- SOF handler ---- */

static void uac1_sof(struct usbd_class_data *const c_data)
{
	const struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;
	struct uac1_ctx *ctx = dev->data;
	const struct usb_ep_descriptor *data_ep;
	const struct usb_ep_descriptor *feedback_ep;
	int as_idx;

	ctx->ops->sof_cb(dev, ctx->user_data);

	for (as_idx = 0; as_idx < cfg->num_ifaces; as_idx++) {
		data_ep = get_as_data_ep(c_data, as_idx);
		if (data_ep && USB_EP_DIR_IS_OUT(data_ep->bEndpointAddress)) {
			schedule_iso_out_read(c_data, data_ep->bEndpointAddress,
				sys_le16_to_cpu(data_ep->wMaxPacketSize),
				cfg->as_terminals[as_idx]);
		}

		feedback_ep = get_as_feedback_ep(c_data, as_idx);
		if (feedback_ep == NULL) {
			continue;
		}

		if (ctx->fb_queued & ctx->fb_double & BIT(as_idx)) {
			continue;
		}

		if (!atomic_test_bit(&ctx->as_active, as_idx)) {
			continue;
		}

		write_explicit_feedback(c_data, feedback_ep->bEndpointAddress,
					cfg->as_terminals[as_idx]);
	}
}

/* ---- Descriptor getter ---- */

static void *uac1_get_desc(struct usbd_class_data *const c_data,
			    const enum usbd_speed speed)
{
	struct device *dev = usbd_class_get_private(c_data);
	const struct uac1_cfg *cfg = dev->config;

	/* UAC1 is Full-Speed only */
	ARG_UNUSED(speed);
	return cfg->fs_descriptors;
}

/* ---- Disable ---- */

static void uac1_disable(struct usbd_class_data *const c_data)
{
	const struct device *dev = usbd_class_get_private(c_data);
	struct uac1_ctx *ctx = dev->data;
	const struct uac1_cfg *cfg = dev->config;
	atomic_val_t as_active;

	as_active = atomic_clear(&ctx->as_active);

	while (as_active) {
		unsigned int as_idx = find_lsb_set(as_active) - 1;

		ctx->ops->terminal_update_cb(dev, cfg->as_terminals[as_idx],
					     0, false, ctx->user_data);
		as_active &= ~BIT(as_idx);
	}
}

/* ---- Init ---- */

static int uac1_init(struct usbd_class_data *const c_data)
{
	const struct device *dev = usbd_class_get_private(c_data);
	struct uac1_ctx *ctx = dev->data;

	if (ctx->ops == NULL) {
		LOG_ERR("Application did not register UAC1 ops");
		return -EINVAL;
	}

	return 0;
}

/* ---- Class API vtable ---- */

struct usbd_class_api uac1_api = {
	.update = uac1_update,
	.control_to_dev = uac1_control_to_dev,
	.control_to_host = uac1_control_to_host,
	.request = uac1_request,
	.sof = uac1_sof,
	.get_desc = uac1_get_desc,
	.disable = uac1_disable,
	.init = uac1_init,
};

/* ---- DT-driven instance instantiation ---- */

#define DEFINE_UAC1_CLASS_DATA(inst)						\
	static struct uac1_ctx uac1_ctx_##inst;					\
	UAC1_DESCRIPTOR_ARRAYS(DT_DRV_INST(inst))				\
	static const struct usb_desc_header *uac1_fs_desc_##inst[] =		\
		UAC1_FS_DESCRIPTOR_PTRS_ARRAY(DT_DRV_INST(inst));		\
	USBD_DEFINE_CLASS(uac1_##inst, &uac1_api,				\
			  (void *)DEVICE_DT_GET(DT_DRV_INST(inst)), NULL);	\
	DEFINE_UAC1_LOOKUP_TABLES(inst)						\
	DEFINE_UAC1_FREQ_TABLES(inst)						\
	static const struct uac1_cfg uac1_cfg_##inst = {			\
		.c_data = &uac1_##inst,						\
		.fs_descriptors = uac1_fs_desc_##inst,				\
		.entity_types = uac1_entity_types_##inst,			\
		.ep_indexes = uac1_ep_indexes_##inst,				\
		.fb_indexes = uac1_fb_indexes_##inst,				\
		.as_terminals = uac1_as_terminals_##inst,			\
		.num_ifaces = ARRAY_SIZE(uac1_ep_indexes_##inst),		\
		.num_entities = ARRAY_SIZE(uac1_entity_types_##inst),		\
	};									\
	BUILD_ASSERT(ARRAY_SIZE(uac1_ep_indexes_##inst) <= 32,			\
		"UAC1 supports up to 32 AS interfaces");			\
	DEVICE_DT_DEFINE(DT_DRV_INST(inst), NULL, NULL,			\
		&uac1_ctx_##inst, &uac1_cfg_##inst,				\
		POST_KERNEL, CONFIG_KERNEL_INIT_PRIORITY_DEVICE,		\
		NULL);

DT_INST_FOREACH_STATUS_OKAY(DEFINE_UAC1_CLASS_DATA)
