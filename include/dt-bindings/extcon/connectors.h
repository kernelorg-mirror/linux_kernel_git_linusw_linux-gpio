#ifndef _DT_BINDINGS_EXTCON_CONNECTORS_H
#define _DT_BINDINGS_EXTCON_CONNECTORS_H

/* USB external connector */
#define EXTCON_USB		1
#define EXTCON_USB_HOST		2
#define EXTCON_CHG_USB_SDP	5	/* Standard Downstream Port */
#define EXTCON_CHG_USB_DCP	6	/* Dedicated Charging Port */
#define EXTCON_CHG_USB_CDP	7	/* Charging Downstream Port */
#define EXTCON_CHG_USB_ACA	8	/* Accessory Charger Adapter */
#define EXTCON_CHG_USB_FAST	9
#define EXTCON_CHG_USB_SLOW	10
#define EXTCON_CHG_WPT		11	/* Wireless Power Transfer */
#define EXTCON_CHG_USB_PD	12	/* USB Power Delivery */
/* Jack external connector */
#define EXTCON_JACK_MICROPHONE	20
#define EXTCON_JACK_HEADPHONE	21
#define EXTCON_JACK_LINE_IN	22
#define EXTCON_JACK_LINE_OUT	23
#define EXTCON_JACK_VIDEO_IN	24
#define EXTCON_JACK_VIDEO_OUT	25
#define EXTCON_JACK_SPDIF_IN	26	/* Sony Philips Digital InterFace */
#define EXTCON_JACK_SPDIF_OUT	27
/* Display external connector */
#define EXTCON_DISP_HDMI	40	/* High-Definition Multimedia Interface */
#define EXTCON_DISP_MHL		41	/* Mobile High-Definition Link */
#define EXTCON_DISP_DVI		42	/* Digital Visual Interface */
#define EXTCON_DISP_VGA		43	/* Video Graphics Array */
#define EXTCON_DISP_DP		44	/* Display Port */
#define EXTCON_DISP_HMD		45	/* Head-Mounted Display */
/* Miscellaneous external connector */
#define EXTCON_DOCK		60
#define EXTCON_JIG		61
#define EXTCON_MECHANICAL	62

#define EXTCON_NUM		63

#endif /* _DT_BINDINGS_EXTCON_CONNECTORS_H */
