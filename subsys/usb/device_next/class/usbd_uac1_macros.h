/*
 * Copyright (c) 2025 Smartbox Assistive Technology Ltd
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ERISKAY_INCLUDE_SUBSYS_USB_DEVICE_NEXT_CLASS_USBD_UAC1_MACROS_H
#define ERISKAY_INCLUDE_SUBSYS_USB_DEVICE_NEXT_CLASS_USBD_UAC1_MACROS_H

/* Macros to generate USB Audio Class 1.0 descriptors from devicetree.
 * The output is raw uint8_t arrays consumed by the device_next USB stack.
 */

#include <zephyr/sys/util.h>
#include <zephyr/usb/usb_ch9.h>

#ifndef ZEPHYR_INCLUDE_USBD_UAC1_MACROS_H_
#define ZEPHYR_INCLUDE_USBD_UAC1_MACROS_H_

#define U16_LE(value) ((value) & 0xFF), (((value) & 0xFF00) >> 8)

/* 3 byte little-endian for UAC1 sample frequency encoding */
#define U24_LE(value)								\
	((value) & 0xFF),							\
	(((value) & 0xFF00) >> 8),						\
	(((value) & 0xFF0000) >> 16)

#define FIRST_INTERFACE_NUMBER			0x00
#define FIRST_IN_EP_ADDR			0x81
#define FIRST_OUT_EP_ADDR			0x01

/* A.4 Audio Interface Class Code */
#define AUDIO					0x01

/* A.5 Audio Interface Subclass Codes */
#define AUDIOCONTROL				0x01
#define AUDIOSTREAMING				0x02

/* A.8 Audio Class-Specific Descriptor Types */
#define CS_INTERFACE				0x24
#define CS_ENDPOINT				0x25

/* A.9 Audio Class-Specific AC Interface Descriptor Subtypes */
#define AC_DESCRIPTOR_HEADER			0x01
#define AC_DESCRIPTOR_INPUT_TERMINAL		0x02
#define AC_DESCRIPTOR_OUTPUT_TERMINAL		0x03
#define AC_DESCRIPTOR_FEATURE_UNIT		0x06

/* A.10 Audio Class-Specific AS Interface Descriptor Subtypes */
#define AS_DESCRIPTOR_GENERAL			0x01
#define AS_DESCRIPTOR_FORMAT_TYPE		0x02

/* A.13 Audio Class-Specific Endpoint Descriptor Subtypes */
#define EP_GENERAL				0x01

/* Format Type Codes (audio10 frmts10.pdf) */
#define FORMAT_TYPE_I				1

/* ---- Entity ID assignment (same pattern as UAC2) ---- */
#define ENTITY_ID(e) UTIL_INC(DT_NODE_CHILD_IDX(e))

#define DESCRIPTOR_NAME(prefix, node) uac1_## prefix ## _ ## node

#define CONNECTED_ENTITY_ID(entity, phandle)					\
	COND_CODE_1(DT_NODE_HAS_PROP(entity, phandle),				\
		(ENTITY_ID(DT_PHANDLE_BY_IDX(entity, phandle, 0))), (0))

/* ---- Spatial locations (UAC1 uses a 16-bit wChannelConfig) ---- */
#define SPATIAL_LOCATION(entity, prop, bit)					\
	(DT_PROP(entity, prop) * BIT(bit))

#define SPATIAL_LOCATIONS_U16(entity)						\
	(SPATIAL_LOCATION(entity, front_left, 0) |				\
	 SPATIAL_LOCATION(entity, front_right, 1) |				\
	 SPATIAL_LOCATION(entity, front_center, 2) |				\
	 SPATIAL_LOCATION(entity, low_frequency_effects, 3) |			\
	 SPATIAL_LOCATION(entity, back_left, 4) |				\
	 SPATIAL_LOCATION(entity, back_right, 5) |				\
	 SPATIAL_LOCATION(entity, front_left_of_center, 6) |			\
	 SPATIAL_LOCATION(entity, front_right_of_center, 7) |			\
	 SPATIAL_LOCATION(entity, back_center, 8) |				\
	 SPATIAL_LOCATION(entity, side_left, 9) |				\
	 SPATIAL_LOCATION(entity, side_right, 10))

#define NUM_SPATIAL_LOCATIONS(entity)						\
	(DT_PROP(entity, front_left) +						\
	 DT_PROP(entity, front_right) +						\
	 DT_PROP(entity, front_center) +					\
	 DT_PROP(entity, low_frequency_effects) +				\
	 DT_PROP(entity, back_left) +						\
	 DT_PROP(entity, back_right) +						\
	 DT_PROP(entity, front_left_of_center) +				\
	 DT_PROP(entity, front_right_of_center) +				\
	 DT_PROP(entity, back_center) +						\
	 DT_PROP(entity, side_left) +						\
	 DT_PROP(entity, side_right))

/* ---- AC Interface Header Descriptor (UAC1 audio10 4.3.2) ---- */
/* bLength = 8 + bInCollection */
#define IS_AUDIOSTREAMING_INTERFACE(node)					\
	DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming)

#define UAC1_NUM_AS_INTERFACES(node)						\
	DT_FOREACH_CHILD_SEP(node, IS_AUDIOSTREAMING_INTERFACE, (+))

#define ENTITY_HEADER(entity)							\
	IF_ENABLED(DT_NODE_HAS_COMPAT(entity, zephyr_uac1_input_terminal), (	\
		INPUT_TERMINAL_DESCRIPTOR(entity)				\
	))									\
	IF_ENABLED(DT_NODE_HAS_COMPAT(entity, zephyr_uac1_output_terminal), (	\
		OUTPUT_TERMINAL_DESCRIPTOR(entity)				\
	))

#define ENTITY_HEADERS(node) DT_FOREACH_CHILD(node, ENTITY_HEADER)
#define ENTITY_HEADERS_LENGTH(node) sizeof((uint8_t []){ENTITY_HEADERS(node)})

/* AC header baInterfaceNr entries: one per AudioStreaming child */
#define AS_BAINTERFACE_ENTRY(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		AS_INTERFACE_NUMBER(node),					\
	))
#define AC_BAINTERFACE_ENTRIES(node)						\
	DT_FOREACH_CHILD(node, AS_BAINTERFACE_ENTRY)

#define AC_INTERFACE_HEADER_DESCRIPTOR(node)					\
	(8 + UAC1_NUM_AS_INTERFACES(node)),	/* bLength */			\
	CS_INTERFACE,				/* bDescriptorType */		\
	AC_DESCRIPTOR_HEADER,			/* bDescriptorSubtype */	\
	U16_LE(0x0100),				/* bcdADC */			\
	U16_LE(8 + UAC1_NUM_AS_INTERFACES(node) +				\
		ENTITY_HEADERS_LENGTH(node)),	/* wTotalLength */		\
	UAC1_NUM_AS_INTERFACES(node),		/* bInCollection */		\
	AC_BAINTERFACE_ENTRIES(node)		/* baInterfaceNr[] */

/* ---- Input Terminal Descriptor (UAC1 audio10 4.3.2.1, 12 bytes) ---- */
#define INPUT_TERMINAL_DESCRIPTOR(entity)					\
	0x0C,					/* bLength */			\
	CS_INTERFACE,				/* bDescriptorType */		\
	AC_DESCRIPTOR_INPUT_TERMINAL,		/* bDescriptorSubtype */	\
	ENTITY_ID(entity),			/* bTerminalID */		\
	U16_LE(DT_PROP(entity, terminal_type)),	/* wTerminalType */		\
	CONNECTED_ENTITY_ID(entity, assoc_terminal),/* bAssocTerminal */	\
	NUM_SPATIAL_LOCATIONS(entity),		/* bNrChannels */		\
	U16_LE(SPATIAL_LOCATIONS_U16(entity)),	/* wChannelConfig */		\
	0x00,					/* iChannelNames */		\
	0x00,					/* iTerminal */

/* ---- Output Terminal Descriptor (UAC1 audio10 4.3.2.2, 9 bytes) ---- */
#define OUTPUT_TERMINAL_DESCRIPTOR(entity)					\
	0x09,					/* bLength */			\
	CS_INTERFACE,				/* bDescriptorType */		\
	AC_DESCRIPTOR_OUTPUT_TERMINAL,		/* bDescriptorSubtype */	\
	ENTITY_ID(entity),			/* bTerminalID */		\
	U16_LE(DT_PROP(entity, terminal_type)),	/* wTerminalType */		\
	CONNECTED_ENTITY_ID(entity, assoc_terminal),/* bAssocTerminal */	\
	CONNECTED_ENTITY_ID(entity, data_source),/* bSourceID */		\
	0x00,					/* iTerminal */

/* ---- Entity header descriptor arrays and pointer helpers ---- */
#define ENTITY_HEADER_ARRAYS(entity)						\
	IF_ENABLED(UTIL_NOT(IS_EMPTY(ENTITY_HEADER(entity))), (			\
		static uint8_t DESCRIPTOR_NAME(ac_entity, entity)[] = {		\
			ENTITY_HEADER(entity)					\
		};								\
	))

#define ENTITY_HEADER_PTRS(entity)						\
	IF_ENABLED(UTIL_NOT(IS_EMPTY(ENTITY_HEADER(entity))), (			\
		(struct usb_desc_header *) &DESCRIPTOR_NAME(ac_entity, entity),	\
	))

#define ENTITY_HEADERS_ARRAYS(node) DT_FOREACH_CHILD(node, ENTITY_HEADER_ARRAYS)
#define ENTITY_HEADERS_PTRS(node)  DT_FOREACH_CHILD(node, ENTITY_HEADER_PTRS)

/* ---- AudioStreaming interface helpers ---- */
#define FIND_AUDIOSTREAMING(node, fn, ...)					\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		fn(node, __VA_ARGS__)))

#define FOR_EACH_AUDIOSTREAMING_INTERFACE(node, fn, ...)			\
	DT_FOREACH_CHILD_VARGS(node, FIND_AUDIOSTREAMING, fn, __VA_ARGS__)

#define COUNT_AS_INTERFACES_BEFORE_IDX(node, idx)				\
	+ 1 * (DT_NODE_CHILD_IDX(node) < idx)

#define AS_INTERFACE_NUMBER(node)						\
	FIRST_INTERFACE_NUMBER + 1 /* AudioControl */ +				\
	FOR_EACH_AUDIOSTREAMING_INTERFACE(DT_PARENT(node),			\
		COUNT_AS_INTERFACES_BEFORE_IDX, DT_NODE_CHILD_IDX(node))

/* Determine direction from linked terminal type */
#define AS_IS_USB_ISO_OUT(node)							\
	DT_NODE_HAS_COMPAT(DT_PROP(node, linked_terminal),			\
		zephyr_uac1_input_terminal)

#define AS_IS_USB_ISO_IN(node)							\
	DT_NODE_HAS_COMPAT(DT_PROP(node, linked_terminal),			\
		zephyr_uac1_output_terminal)

/* Channel cluster comes from linked terminal for input, or from data-source
 * chain for output. For UAC1 speaker-out the input terminal IS the linked one.
 */
#define AS_CHANNEL_CLUSTER(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(DT_PROP(node, linked_terminal),		\
		zephyr_uac1_input_terminal), (					\
			DT_PROP(node, linked_terminal)				\
	))									\
	IF_ENABLED(DT_NODE_HAS_COMPAT(DT_PROP(node, linked_terminal),		\
		zephyr_uac1_output_terminal), (					\
			DT_PROP(DT_PROP(node, linked_terminal), data_source)	\
	))

#define AS_NUM_CHANNELS(node) NUM_SPATIAL_LOCATIONS(AS_CHANNEL_CLUSTER(node))

/* Max sampling frequency from the sorted array */
#define AS_MAX_FREQUENCY(node)							\
	DT_PROP_BY_IDX(node, sampling_frequencies,				\
		UTIL_DEC(DT_PROP_LEN(node, sampling_frequencies)))

/* ---- Feedback endpoint detection ---- */
#define AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node)					\
	UTIL_AND(AS_IS_USB_ISO_OUT(node),					\
		UTIL_NOT(DT_PROP(node, implicit_feedback)))

#define AS_NUM_ENDPOINTS(node)							\
	(1 + AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node))

/* ---- Endpoint address computation ---- */
#define COUNT_AS_OUT_EP_BEFORE(node, idx)					\
	+ AS_IS_USB_ISO_OUT(node) * (DT_NODE_CHILD_IDX(node) < idx)

#define COUNT_AS_IN_EP_BEFORE(node, idx)					\
	+ (AS_IS_USB_ISO_IN(node) + AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node))	\
		* (DT_NODE_CHILD_IDX(node) < idx)

#define AS_NEXT_OUT_EP_ADDR(node)						\
	FIRST_OUT_EP_ADDR +							\
	FOR_EACH_AUDIOSTREAMING_INTERFACE(DT_PARENT(node),			\
		COUNT_AS_OUT_EP_BEFORE, DT_NODE_CHILD_IDX(node))

#define AS_NEXT_IN_EP_ADDR(node)						\
	FIRST_IN_EP_ADDR +							\
	FOR_EACH_AUDIOSTREAMING_INTERFACE(DT_PARENT(node),			\
		COUNT_AS_IN_EP_BEFORE, DT_NODE_CHILD_IDX(node))

#define AS_DATA_EP_ADDR(node)							\
	COND_CODE_1(AS_IS_USB_ISO_OUT(node),					\
		(AS_NEXT_OUT_EP_ADDR(node)),					\
		(AS_NEXT_IN_EP_ADDR(node)))

/* Max packet = channels * subframe_size * (max_freq/1000 + 1_for_async) */
#define AS_FS_DATA_EP_MPS(node)							\
	(AS_NUM_CHANNELS(node) *						\
	 DT_PROP(node, subframe_size) *						\
	 (ROUND_UP(AS_MAX_FREQUENCY(node), 1000) / 1000 + 1))

/* Asynchronous sync type = 0x01<<2 = 0x04, ORed with ISO = 0x01 -> 0x05 */
#define AS_DATA_EP_ATTR(node) (USB_EP_TYPE_ISO | (0x01 << 2))

/* ---- Standard AS Interface Descriptor (alt 0 and alt 1) ---- */
#define AS_INTERFACE_DESCRIPTOR(node, alternate, numep)				\
	0x09,					/* bLength */			\
	USB_DESC_INTERFACE,			/* bDescriptorType */		\
	AS_INTERFACE_NUMBER(node),		/* bInterfaceNumber */		\
	alternate,				/* bAlternateSetting */		\
	numep,					/* bNumEndpoints */		\
	AUDIO,					/* bInterfaceClass */		\
	AUDIOSTREAMING,				/* bInterfaceSubClass */	\
	0x00,					/* bInterfaceProtocol */	\
	0x00,					/* iInterface */

/* ---- Class-Specific AS General Descriptor (UAC1 audio10 4.5.2, 7 bytes) ---- */
#define AS_CS_GENERAL_DESCRIPTOR(node)						\
	0x07,					/* bLength */			\
	CS_INTERFACE,				/* bDescriptorType */		\
	AS_DESCRIPTOR_GENERAL,			/* bDescriptorSubtype */	\
	CONNECTED_ENTITY_ID(node, linked_terminal),/* bTerminalLink */		\
	0x01,					/* bDelay (1 frame) */		\
	U16_LE(0x0001),				/* wFormatTag (PCM) */

/* ---- Format Type I Descriptor (UAC1 frmts10 2.2.3.1) ---- */
/* bLength = 8 + 3*n  where n = bSamFreqType (number of discrete freqs) */
#define SAMFREQ_ENTRY(node, prop, idx) U24_LE(DT_PROP_BY_IDX(node, prop, idx)),

#define FORMAT_TYPE_I_DESCRIPTOR(node)						\
	(8 + 3 * DT_PROP_LEN(node, sampling_frequencies)),/* bLength */		\
	CS_INTERFACE,				/* bDescriptorType */		\
	AS_DESCRIPTOR_FORMAT_TYPE,		/* bDescriptorSubtype */	\
	FORMAT_TYPE_I,				/* bFormatType */		\
	AS_NUM_CHANNELS(node),			/* bNrChannels */		\
	DT_PROP(node, subframe_size),		/* bSubframeSize */		\
	DT_PROP(node, bit_resolution),		/* bBitResolution */		\
	DT_PROP_LEN(node, sampling_frequencies),/* bSamFreqType */		\
	DT_FOREACH_PROP_ELEM(node, sampling_frequencies, SAMFREQ_ENTRY)

/* ---- Standard AS ISO Data Endpoint Descriptor (UAC1: 9 bytes, Table 4-20) ---- */
#define AS_DATA_EP_SYNCH_ADDR(node)						\
	COND_CODE_1(AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node),			\
		(AS_NEXT_IN_EP_ADDR(node)),					\
		(0x00))

#define AS_STD_DATA_EP_DESCRIPTOR(node)						\
	0x09,					/* bLength */			\
	USB_DESC_ENDPOINT,			/* bDescriptorType */		\
	AS_DATA_EP_ADDR(node),			/* bEndpointAddress */		\
	AS_DATA_EP_ATTR(node),			/* bmAttributes */		\
	U16_LE(AS_FS_DATA_EP_MPS(node)),	/* wMaxPacketSize */		\
	0x01,					/* bInterval */			\
	0x00,					/* bRefresh */			\
	AS_DATA_EP_SYNCH_ADDR(node),		/* bSynchAddress */

/* ---- Class-Specific AS ISO Data Endpoint Descriptor (UAC1 4.6.1.2, 7 bytes) ---- */
#define AS_CS_DATA_EP_DESCRIPTOR(node)						\
	0x07,					/* bLength */			\
	CS_ENDPOINT,				/* bDescriptorType */		\
	EP_GENERAL,				/* bDescriptorSubtype */	\
	0x01,					/* bmAttributes (SampFreq) */	\
	0x00,					/* bLockDelayUnits */		\
	U16_LE(0x0000),				/* wLockDelay */

/* ---- Explicit Feedback Endpoint Descriptor (UAC1: 9 bytes, Table 4-22) ---- */
#define AS_FEEDBACK_EP_DESCRIPTOR(node)						\
	0x09,					/* bLength */			\
	USB_DESC_ENDPOINT,			/* bDescriptorType */		\
	AS_NEXT_IN_EP_ADDR(node),		/* bEndpointAddress */		\
	USB_EP_TYPE_ISO,			/* bmAttributes (iso only) */	\
	U16_LE(0x0003),				/* wMaxPacketSize (3 bytes) */	\
	0x01,					/* bInterval */			\
	0x07,					/* bRefresh (2^7=128ms) */	\
	0x00,					/* bSynchAddress */

/* ---- IAD ---- */
#define UAC1_NUM_INTERFACES(node)						\
	(1 + UAC1_NUM_AS_INTERFACES(node))

#define UAC1_IAD_DESCRIPTOR(node)						\
	0x08,					/* bLength */			\
	USB_DESC_INTERFACE_ASSOC,		/* bDescriptorType */		\
	FIRST_INTERFACE_NUMBER,			/* bFirstInterface */		\
	UAC1_NUM_INTERFACES(node),		/* bInterfaceCount */		\
	AUDIO,					/* bFunctionClass */		\
	0x00,					/* bFunctionSubclass */		\
	0x00,					/* bFunctionProtocol */		\
	0x00,					/* iFunction */

/* ---- AC Standard Interface ---- */
#define AC_INTERFACE_DESCRIPTOR(node)						\
	0x09,					/* bLength */			\
	USB_DESC_INTERFACE,			/* bDescriptorType */		\
	FIRST_INTERFACE_NUMBER,			/* bInterfaceNumber */		\
	0x00,					/* bAlternateSetting */		\
	0x00,					/* bNumEndpoints */		\
	AUDIO,					/* bInterfaceClass */		\
	AUDIOCONTROL,				/* bInterfaceSubClass */	\
	0x00,					/* bInterfaceProtocol */	\
	0x00,					/* iInterface */

/* ======================================================================
 * Descriptor byte-array and pointer-array assembly
 * ====================================================================== */

/* Per-AudioStreaming descriptor arrays */
#define AS_DESCRIPTOR_ARRAYS(node)						\
	static uint8_t DESCRIPTOR_NAME(as_if_alt0, node)[] = {			\
		AS_INTERFACE_DESCRIPTOR(node, 0, 0)				\
	};									\
	static uint8_t DESCRIPTOR_NAME(as_if_alt1, node)[] = {			\
		AS_INTERFACE_DESCRIPTOR(node, 1, AS_NUM_ENDPOINTS(node))	\
	};									\
	static uint8_t DESCRIPTOR_NAME(as_cs_general, node)[] = {		\
		AS_CS_GENERAL_DESCRIPTOR(node)					\
	};									\
	static uint8_t DESCRIPTOR_NAME(as_format, node)[] = {			\
		FORMAT_TYPE_I_DESCRIPTOR(node)					\
	};									\
	static uint8_t DESCRIPTOR_NAME(std_data_ep, node)[] = {			\
		AS_STD_DATA_EP_DESCRIPTOR(node)					\
	};									\
	static uint8_t DESCRIPTOR_NAME(cs_data_ep, node)[] = {			\
		AS_CS_DATA_EP_DESCRIPTOR(node)					\
	};									\
	IF_ENABLED(AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node), (			\
		static uint8_t DESCRIPTOR_NAME(feedback_ep, node)[] = {		\
			AS_FEEDBACK_EP_DESCRIPTOR(node)				\
		};								\
	))

/* Pointer entries for one AudioStreaming interface */
#define AS_DESCRIPTOR_PTRS(node)						\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(as_if_alt0, node),		\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(as_if_alt1, node),		\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(as_cs_general, node),	\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(as_format, node),		\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(std_data_ep, node),		\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(cs_data_ep, node),		\
	IF_ENABLED(AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node), (			\
		(struct usb_desc_header *) &DESCRIPTOR_NAME(feedback_ep, node),	\
	))

/* Iterate children — only process audio-streaming nodes */
#define AS_DESCRIPTOR_ARRAYS_IF(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		AS_DESCRIPTOR_ARRAYS(node)))

#define AS_DESCRIPTOR_PTRS_IF(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		AS_DESCRIPTOR_PTRS(node)))

/* ---- Top-level assembly ---- */
#define UAC1_DESCRIPTOR_ARRAYS(node)						\
	static uint8_t DESCRIPTOR_NAME(iad, node)[] = {				\
		UAC1_IAD_DESCRIPTOR(node)					\
	};									\
	static uint8_t DESCRIPTOR_NAME(ac_interface, node)[] = {		\
		AC_INTERFACE_DESCRIPTOR(node)					\
	};									\
	static uint8_t DESCRIPTOR_NAME(ac_header, node)[] = {			\
		AC_INTERFACE_HEADER_DESCRIPTOR(node)				\
	};									\
	ENTITY_HEADERS_ARRAYS(node)						\
	DT_FOREACH_CHILD(node, AS_DESCRIPTOR_ARRAYS_IF)

#define UAC1_FS_DESCRIPTOR_PTRS(node)						\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(iad, node),			\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(ac_interface, node),	\
	(struct usb_desc_header *) &DESCRIPTOR_NAME(ac_header, node),		\
	ENTITY_HEADERS_PTRS(node)						\
	DT_FOREACH_CHILD(node, AS_DESCRIPTOR_PTRS_IF)				\
	NULL

#define UAC1_FS_DESCRIPTOR_PTRS_ARRAY(node)					\
	{UAC1_FS_DESCRIPTOR_PTRS(node)}

/* ---- Endpoint index computation (for the class driver) ---- */
/* Count how many pointer entries precede the AS descriptors */
#define UAC1_AC_PTRS_COUNT(node)						\
	(3 /* iad + ac_iface + ac_header */					\
	+ sizeof((struct usb_desc_header *[]){ENTITY_HEADERS_PTRS(node)})	\
	  / sizeof(struct usb_desc_header *))

/* Per-AS: 6 ptrs without feedback, 7 with feedback */
#define AS_PTRS_COUNT(node) (6 + AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node))

#define COUNT_AS_PTRS_BEFORE_IDX(node, idx)					\
	+ AS_PTRS_COUNT(node) * (DT_NODE_CHILD_IDX(node) < idx)

/* Index of the standard data EP descriptor in the pointer array */
#define UAC1_DESCRIPTOR_AS_DATA_EP_INDEX(node)					\
	UAC1_AC_PTRS_COUNT(DT_PARENT(node)) +					\
	FOR_EACH_AUDIOSTREAMING_INTERFACE(DT_PARENT(node),			\
		COUNT_AS_PTRS_BEFORE_IDX, DT_NODE_CHILD_IDX(node))		\
	+ 4 /* skip: alt0, alt1, cs_general, format -> data_ep is 5th */

/* Index of the feedback EP descriptor in the pointer array */
#define UAC1_DESCRIPTOR_AS_FEEDBACK_EP_INDEX(node)				\
	UAC1_DESCRIPTOR_AS_DATA_EP_INDEX(node) + 2 /* skip data_ep + cs_ep */

/* ---- Entity type definitions (mirrors UAC2 pattern) ---- */
typedef enum {
	UAC1_ENTITY_TYPE_INVALID,
	UAC1_ENTITY_TYPE_INPUT_TERMINAL,
	UAC1_ENTITY_TYPE_OUTPUT_TERMINAL,
} uac1_entity_type_t;

#define DEFINE_UAC1_ENTITY_TYPES(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_input_terminal), (	\
		UAC1_ENTITY_TYPE_INPUT_TERMINAL					\
	))									\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_output_terminal), (	\
		UAC1_ENTITY_TYPE_OUTPUT_TERMINAL				\
	))									\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		UAC1_ENTITY_TYPE_INVALID					\
	))									\
	,

#define DEFINE_UAC1_AS_EP_INDEXES(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		UAC1_DESCRIPTOR_AS_DATA_EP_INDEX(node),				\
	))

#define DEFINE_UAC1_AS_FB_INDEXES(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		COND_CODE_1(AS_HAS_EXPLICIT_FEEDBACK_ENDPOINT(node),		\
			(UAC1_DESCRIPTOR_AS_FEEDBACK_EP_INDEX(node),),		\
			(0,))							\
	))

#define DEFINE_UAC1_AS_TERMINALS(node)						\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		ENTITY_ID(DT_PROP(node, linked_terminal)),			\
	))

#define DEFINE_UAC1_LOOKUP_TABLES(i)						\
	static const uac1_entity_type_t uac1_entity_types_##i[] = {		\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(i, DEFINE_UAC1_ENTITY_TYPES)	\
	};									\
	static const uint16_t uac1_ep_indexes_##i[] = {				\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(i, DEFINE_UAC1_AS_EP_INDEXES)	\
	};									\
	static const uint16_t uac1_fb_indexes_##i[] = {				\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(i, DEFINE_UAC1_AS_FB_INDEXES)	\
	};									\
	static const uint8_t uac1_as_terminals_##i[] = {			\
		DT_INST_FOREACH_CHILD_STATUS_OKAY(i, DEFINE_UAC1_AS_TERMINALS)	\
	};

/* Sampling frequencies lookup (for endpoint sample rate control) */
#define FREQ_TABLE_NAME(node, i) uac1_frequencies_##i##_##node

#define DEFINE_UAC1_AS_FREQ_TABLE(node, i)					\
	IF_ENABLED(DT_NODE_HAS_COMPAT(node, zephyr_uac1_audio_streaming), (	\
		static const uint32_t FREQ_TABLE_NAME(node, i)[] =		\
			DT_PROP(node, sampling_frequencies);			\
	))

#define DEFINE_UAC1_FREQ_TABLES(i)						\
	DT_INST_FOREACH_CHILD_STATUS_OKAY_VARGS(i,				\
		DEFINE_UAC1_AS_FREQ_TABLE, i)

#endif /* ZEPHYR_INCLUDE_USBD_UAC1_MACROS_H_ */


#endif /* ERISKAY_INCLUDE_SUBSYS_USB_DEVICE_NEXT_CLASS_USBD_UAC1_MACROS_H */
