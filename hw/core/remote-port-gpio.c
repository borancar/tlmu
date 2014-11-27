/*
 * Copyright (c) 2013 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 * Written by Peter Crosthwaite <peter.crosthwaite@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "hw/sysbus.h"
#include "sysemu/sysemu.h"
#include "sysemu/dma.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "hw/qdev.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"

#define TYPE_REMOTE_PORT_GPIO "remote-port-gpio"
#define REMOTE_PORT_GPIO(obj) \
        OBJECT_CHECK(RemotePortGPIO, (obj), TYPE_REMOTE_PORT_GPIO)

#define MAX_GPIOS 32

typedef struct RemotePortGPIO {
    /* private */
    SysBusDevice parent;
    /* public */

    uint16_t num_gpios;
    qemu_irq *gpio_out;

    uint64_t current_id;

    uint32_t rp_dev;
    struct RemotePort *rp;
} RemotePortGPIO;

static void rp_gpio_handler(void *opaque, int irq, int level)
{
    RemotePortGPIO *s = opaque;
    struct rp_pkt pkt;
    size_t len;
    int64_t clk = rp_normalized_vmclk(s->rp);

    len = rp_encode_interrupt(s->current_id++, s->rp_dev, &pkt.interrupt, clk,
                              irq, 0, level);
    rp_write(s->rp, (void *)&pkt, len);
}

static void rp_gpio_interrupt(RemotePortDevice *s, struct rp_pkt *pkt)
{
    RemotePortGPIO *rpg = REMOTE_PORT_GPIO(s);

    qemu_set_irq(rpg->gpio_out[pkt->interrupt.line], pkt->interrupt.val);
}

static void rp_gpio_realize(DeviceState *dev, Error **errp)
{
    RemotePortGPIO *s = REMOTE_PORT_GPIO(dev);
    unsigned int i;

    s->gpio_out = g_new0(qemu_irq, s->num_gpios);
    qdev_init_gpio_out(dev, s->gpio_out, s->num_gpios);
    qdev_init_gpio_in(dev, rp_gpio_handler, s->num_gpios);

    for (i = 0; i < s->num_gpios; i++) {
        sysbus_init_irq(SYS_BUS_DEVICE(s), &s->gpio_out[i]);
    }
}

static void rp_gpio_init(Object *obj)
{
    RemotePortGPIO *rpms = REMOTE_PORT_GPIO(obj);

    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortGPIO, rp_dev, 0),
    DEFINE_PROP_UINT16("num-gpios", RemotePortGPIO, num_gpios, 16),
    DEFINE_PROP_END_OF_LIST(),
};

static void rp_gpio_class_init(ObjectClass *oc, void *data)
{
    RemotePortDeviceClass *rpdc = REMOTE_PORT_DEVICE_CLASS(oc);
    DeviceClass *dc = DEVICE_CLASS(oc);

    rpdc->ops[RP_CMD_interrupt] = rp_gpio_interrupt;
    dc->realize = rp_gpio_realize;
    dc->props = rp_properties;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_GPIO,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortGPIO),
    .instance_init = rp_gpio_init,
    .class_init    = rp_gpio_class_init,
    .interfaces    = (InterfaceInfo[]) {
        { TYPE_REMOTE_PORT_DEVICE },
        { },
    },
};

static void rp_register_types(void)
{
    type_register_static(&rp_info);
}

type_init(rp_register_types)
