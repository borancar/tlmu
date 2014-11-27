/*
 * QEMU remote port memory master.
 *
 * Copyright (c) 2014 Xilinx Inc
 * Written by Edgar E. Iglesias <edgar.iglesias@xilinx.com>
 *
 * This code is licensed under the GNU GPL.
 */

#include "sysemu/sysemu.h"
#include "qemu/log.h"
#include "qapi/qmp/qerror.h"
#include "hw/sysbus.h"

#include "hw/remote-port-proto.h"
#include "hw/remote-port-device.h"

#ifndef REMOTE_PORT_ERR_DEBUG
#define REMOTE_PORT_DEBUG_LEVEL 0
#else
#define REMOTE_PORT_DEBUG_LEVEL 1
#endif

#define DB_PRINT_L(level, ...) do { \
    if (REMOTE_PORT_DEBUG_LEVEL > level) { \
        fprintf(stderr,  ": %s: ", __func__); \
        fprintf(stderr, ## __VA_ARGS__); \
    } \
} while (0);

#define TYPE_REMOTE_PORT_MEMORY_MASTER "remote-port-memory-master"
#define REMOTE_PORT_MEMORY_MASTER(obj) \
        OBJECT_CHECK(RemotePortMemoryMaster, (obj), \
                     TYPE_REMOTE_PORT_MEMORY_MASTER)

#define REMOTE_PORT_MEMORY_MASTER_PARENT_CLASS \
    object_class_get_parent( \
            object_class_by_name(TYPE_REMOTE_PORT_MEMORY_MASTER))

typedef struct RemotePortMemoryMaster RemotePortMemoryMaster;

typedef struct RemotePortMap {
    RemotePortMemoryMaster *parent;
    MemoryRegion iomem;
    uint64_t offset;
} RemotePortMap;

struct RemotePortMemoryMaster {
    /* private */
    SysBusDevice parent;

    RemotePortMap *mmaps;

    /* public */
    struct {
        uint32_t num_maps;
        uint32_t num_offsets;
        uint64_t *mapsize;
        uint64_t *mapoffset;
    } cfg;
    uint32_t rp_dev;
    struct RemotePort *rp;
};

static uint64_t rp_io_read(void *opaque, hwaddr addr, unsigned size)
{
    RemotePortMap *map = opaque;
    RemotePortMemoryMaster *s = map->parent;
    struct rp_pkt_busaccess pkt;
    uint64_t value = 0;
    int64_t clk;
    int64_t rclk;
    uint8_t *data;
    uint32_t id;
    int len;
    int i;
    RemotePortDynPkt rsp;

    DB_PRINT_L(1, "\n");
    clk = rp_normalized_vmclk(s->rp);
    id = rp_new_id(s->rp);
    len = rp_encode_read(id, s->rp_dev, &pkt, clk, addr + map->offset, 0, size,
                         0, size);

    rp_rsp_mutex_lock(s->rp);
    rp_write(s->rp, (void *) &pkt, len);

    rsp = rp_wait_resp(s->rp);
    /* We dont support out of order answers yet.  */
    assert(rsp.pkt->hdr.id == be32_to_cpu(pkt.hdr.id));

    data = rp_busaccess_dataptr(&rsp.pkt->busaccess);
    for (i = 0; i < size; i++) {
        value |= data[i] << (i *8);
    }
    rclk = rsp.pkt->busaccess.timestamp;
    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, clk, rclk);

    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);
    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 "\n", addr, value);
    return value;
}

static void rp_io_write(void *opaque, hwaddr addr, uint64_t value,
                        unsigned size)
{
    RemotePortMap *map = opaque;
    RemotePortMemoryMaster *s = map->parent;
    int64_t clk;
    int64_t rclk;
    uint32_t id;
    RemotePortDynPkt rsp;

    struct  {
        struct rp_pkt_busaccess pkt;
        uint8_t data[8];
    } pay;
    int i;
    int len;

    DB_PRINT_L(0, "addr: %" HWADDR_PRIx " data: %" PRIx64 "\n", addr, value);

    for (i = 0; i < 8; i++) {
        pay.data[i] = value >> (i * 8);
    }

    assert(size <= 8);
    clk = rp_normalized_vmclk(s->rp);
    id = rp_new_id(s->rp);
    len = rp_encode_write(id, s->rp_dev, &pay.pkt, clk, addr + map->offset, 0,
                          size, 0, size);

    rp_rsp_mutex_lock(s->rp);

    rp_write(s->rp, (void *) &pay, len + size);

    rsp = rp_wait_resp(s->rp);

    /* We dont support out of order answers yet.  */
    assert(rsp.pkt->hdr.id == be32_to_cpu(pay.pkt.hdr.id));
    rclk = rsp.pkt->busaccess.timestamp;
    rp_dpkt_invalidate(&rsp);
    rp_rsp_mutex_unlock(s->rp);
    rp_sync_vmclock(s->rp, clk, rclk);
    /* Reads are sync-points, roll the sync timer.  */
    rp_restart_sync_timer(s->rp);
    rp_leave_iothread(s->rp);
    DB_PRINT_L(1, "\n");
}

static const MemoryRegionOps rp_ops = {
    .read = rp_io_read,
    .write = rp_io_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
};

static void rp_memory_master_realize(DeviceState *dev, Error **errp)
{
    RemotePortMemoryMaster *s = REMOTE_PORT_MEMORY_MASTER(dev);
    int i;

    assert(s->cfg.num_maps == s->cfg.num_offsets);
    s->mmaps = g_new0(typeof(*s->mmaps), s->cfg.num_maps);
    for (i = 0; i < s->cfg.num_maps; i++) {
        char *name = g_strdup_printf("rp-%d", i);

        s->mmaps[i].offset = s->cfg.mapoffset[i];
        memory_region_init_io(&s->mmaps[i].iomem, OBJECT(dev), &rp_ops,
                              &s->mmaps[i], name, s->cfg.mapsize[i]);
        sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmaps[i].iomem);
        s->mmaps[i].parent = s;
        g_free(name);
    }
}

static void rp_memory_master_init(Object *obj)
{
    RemotePortMemoryMaster *rpms = REMOTE_PORT_MEMORY_MASTER(obj);
    object_property_add_link(obj, "rp-adaptor0", "remote-port",
                             (Object **)&rpms->rp,
                             qdev_prop_allow_set_link_before_realize,
                             OBJ_PROP_LINK_UNREF_ON_RELEASE,
                             &error_abort);
}

static Property rp_properties[] = {
    DEFINE_PROP_UINT32("rp-chan0", RemotePortMemoryMaster, rp_dev, 0),
    DEFINE_PROP_ARRAY("mapsize", RemotePortMemoryMaster, cfg.num_maps,
                      cfg.mapsize, qdev_prop_uint64, uint64_t),
    DEFINE_PROP_ARRAY("mapoffset", RemotePortMemoryMaster, cfg.num_offsets,
                      cfg.mapoffset, qdev_prop_uint64, uint64_t),
    DEFINE_PROP_END_OF_LIST()
};

static void rp_memory_master_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    dc->realize = rp_memory_master_realize;
    dc->props = rp_properties;
}

static const TypeInfo rp_info = {
    .name          = TYPE_REMOTE_PORT_MEMORY_MASTER,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(RemotePortMemoryMaster),
    .instance_init = rp_memory_master_init,
    .class_init    = rp_memory_master_class_init,
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
