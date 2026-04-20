/*
 * Copyright (c) 2025 Smartbox Assistive Technology Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ERISKAY_INCLUDE_INCLUDE_ZEPHYR_USB_CLASS_USBD_UAC1_H
#define ERISKAY_INCLUDE_INCLUDE_ZEPHYR_USB_CLASS_USBD_UAC1_H

/**
 * @file
 * @brief USB Audio Class 1 device public header
 *
 * The audio device itself is modelled with devicetree zephyr,uac1 compatible.
 */

#ifndef ZEPHYR_INCLUDE_USB_CLASS_USBD_UAC1_H_
#define ZEPHYR_INCLUDE_USB_CLASS_USBD_UAC1_H_

#include <zephyr/device.h>

/**
 * @brief Get entity ID from devicetree node
 * @param node node identifier
 */
#define UAC1_ENTITY_ID(node)							\
	({									\
		BUILD_ASSERT(DT_NODE_HAS_COMPAT(DT_PARENT(node), zephyr_uac1));	\
		UTIL_INC(DT_NODE_CHILD_IDX(node));				\
	})

/**
 * @brief USB Audio 1 application event handlers
 *
 * Mirrors the UAC2 callback structure for easy porting. The main difference
 * is no clock source callbacks (UAC1 has no clock entities) and feedback is
 * always Q10.14 in 3 bytes at Full-Speed.
 */
struct uac1_ops {
	/**
	 * @brief Start of Frame callback (mandatory)
	 */
	void (*sof_cb)(const struct device *dev, void *user_data);

	/**
	 * @brief Terminal update callback (mandatory)
	 *
	 * @param terminal Terminal ID linked to AudioStreaming interface
	 * @param enabled True if host enabled terminal, False otherwise
	 * @param microframes Always false for UAC1 (Full-Speed only)
	 */
	void (*terminal_update_cb)(const struct device *dev, uint8_t terminal,
				   bool enabled, bool microframes,
				   void *user_data);

	/**
	 * @brief Get receive buffer (mandatory for OUT endpoints)
	 */
	void *(*get_recv_buf)(const struct device *dev, uint8_t terminal,
			      uint16_t size, void *user_data);

	/**
	 * @brief Data received callback (mandatory for OUT endpoints)
	 */
	void (*data_recv_cb)(const struct device *dev, uint8_t terminal,
			     void *buf, uint16_t size, void *user_data);

	/**
	 * @brief Transmit buffer release (mandatory if calling usbd_uac1_send)
	 */
	void (*buf_release_cb)(const struct device *dev, uint8_t terminal,
			       void *buf, void *user_data);

	/**
	 * @brief Get explicit feedback value
	 *
	 * Must return Q10.14 value on 24 LSBs (Full-Speed UAC1).
	 */
	uint32_t (*feedback_cb)(const struct device *dev, uint8_t terminal,
				void *user_data);
};

/**
 * @brief Set UAC1 callbacks
 */
void usbd_uac1_set_ops(const struct device *dev,
			const struct uac1_ops *ops, void *user_data);

/**
 * @brief Send audio data to host (for IN terminals)
 */
int usbd_uac1_send(const struct device *dev, uint8_t terminal,
		    void *data, uint16_t size);

#endif /* ZEPHYR_INCLUDE_USB_CLASS_USBD_UAC1_H_ */


#endif /* ERISKAY_INCLUDE_INCLUDE_ZEPHYR_USB_CLASS_USBD_UAC1_H */
