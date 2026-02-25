

#include "qemu/osdep.h"
#include "qemu/module.h"
#include "qemu/sockets.h"
#include "hw/hw.h"
#include "hw/qdev-properties.h"
#include "qapi/error.h"
#include "qemu/error-report.h"

#include "hw/misc/efizzer.h"

#include "io/channel.h"

//                                  TAG        EVENT TYPE           LENGTH
#define MMEP_HIT_EVENT_HEADER  ((0UL << 60) | (1UL << 12) | MMEP_HIT_EVENT_MSGLEN)
#define MMEP_MOD_EVENT_HEADER  ((0UL << 60) | (2UL << 12) | MMEP_MOD_EVENT_MSGLEN)

static QIOChannelSocket *efizzer_sock_connect(const char* sock_addr, Error **errp)
{
    SocketAddress     *addr;
    QIOChannelSocket  *sock_io;

    /* Превращаем путь к сокету в SocketAddress */
    addr = socket_parse(sock_addr, errp);
    if (!addr) {

      return NULL;
    }

    /* Создаем объект канала */
    sock_io = qio_channel_socket_new();

    /* Пытаемся подключится к сокету */
    if (qio_channel_socket_connect_sync(sock_io, addr, errp) < 0) {

      object_unref(OBJECT(sock_io));
      qapi_free_SocketAddress(addr);
      return NULL;
    }

    /* Освобождаем память, занятую структурой адреса */
    qapi_free_SocketAddress(addr);
    /* Возвращаем созданный сокет */
    return sock_io;
}

static void efizzer_sock_write(EfizzerState *s, const void *buf, size_t len)
{
    Error *err = NULL;
    const char *err_msg = NULL;

    /* Проверяем, что сокет открыт */
    if (!(s->sock_io)) {

      hw_error("efizzer,socket,error: Have no socket!");
      return;
    }

    /* Пишем в сокет переданное сообщение */
    if (qio_channel_write_all(QIO_CHANNEL(s->sock_io), buf, len, &err) < 0) {

      err_msg = error_get_pretty(err);
      if (err_msg != NULL) {
        hw_error("efizzer,socket,error: %s", err_msg);
      } else {
        hw_error("efizzer,socket,error: socket write failed!");
      }
    }

    if (err != NULL) {
      error_free(err);
    }
}

static void efizzer_sock_close(EfizzerState *s)
{

    if (s->sock_io != NULL) {

      object_unref(OBJECT(s->sock_io));
      s->sock_io = NULL;
    }
}

// ====================================================================

static void efizzer_emit_hit_event(EfizzerState *s)
{
    uint64_t header = MMEP_HIT_EVENT_HEADER;
    uint8_t  *msg   = s->hitEventBuf;

    memcpy((void *)msg, (void *)&header, 8);
    memcpy((void *)&msg[8], (void *)&s->regw_hit, 8);
    efizzer_sock_write(s, (void *)msg, MMEP_HIT_EVENT_MSGLEN);
}

static void efizzer_emit_mod_event(EfizzerState *s)
{
    uint64_t header = MMEP_MOD_EVENT_HEADER;
    uint8_t  *msg   = s->modEventBuf;

    memcpy((void *)msg, (void *)&header, 8);
    memcpy((void *)(&msg[8]), (void *)(&s->regw_modguid), 16);
    memcpy((void *)(&msg[24]), (void *)(&s->regw_modsize), 8);
    memcpy((void *)(&msg[32]), (void *)(&s->regw_modaddr), 8);
    efizzer_sock_write(s, (void *)msg, MMEP_MOD_EVENT_MSGLEN);
}

// ====================================================================

static void efizzer_mmio_write(void *opaque, hwaddr offset,
                               uint64_t value, unsigned size)
{
    EfizzerState *s = opaque;

    if (size != 8) {
      return;
    }


    switch (offset) {

      case MMEP_REG_HIT:
        /* Генерируем событие покрытия */
        s->regw_hit = value;
        efizzer_emit_hit_event(s);
        break;

      case MMEP_REG_MODGUID:
        memcpy(&(s->regw_modguid[0]), &value, sizeof(value));
        break;

      case MMEP_REG_MODGUID + 8:
        memcpy(&(s->regw_modguid[8]), &value, sizeof(value));
        break;

      case MMEP_REG_MODSIZE:
        s->regw_modsize = value;
        break;

      case MMEP_REG_MODADDR:
        /* Генерируем событие загрузки модуля */
        s->regw_modaddr = value;
        efizzer_emit_mod_event(s);
        break;

      default:
        break;
    }
}

static uint64_t efizzer_mmio_read(void *opaque, hwaddr offset, unsigned size)
{
   return 0;
}

static const MemoryRegionOps efizzer_mmio_ops = {
    .read = efizzer_mmio_read,
    .write = efizzer_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 8,
    .valid.max_access_size = 8,
    .impl.min_access_size = 8,
    .impl.max_access_size = 8,
};

// ====================================================================

static void efizzer_realize(DeviceState *dev, Error **errp)
{
    EfizzerState *s = EFIZZER(dev);

    /* Создаем регион памяти */
    memory_region_init_io(&(s->mmio), OBJECT(dev), &efizzer_mmio_ops, 
                          s, TYPE_EFIZZER, MMEP_MMIO_SIZE);

    sysbus_init_mmio(SYS_BUS_DEVICE(s), &s->mmio);

    /* Настраиваем сокет */
    s->sock_io = NULL;

    /* Обязательно должно присутствовать свойство "socket" */
    if (!(s->sock_addr)) {
      hw_error("efizzer,property,error: the socket property is needed!");
      return;
    }

    /* Подключаем сокет */
    s->sock_io = efizzer_sock_connect(s->sock_addr, errp);

    info_report("Устройство создано?\n");
}

static void efizzer_unrealize(DeviceState *dev)
{
    EfizzerState *s = EFIZZER(dev);

    efizzer_sock_close(s);
}

/* Описываем свойство "socket", чтобы настраивать его */
static Property efizzer_properties[] = {
    DEFINE_PROP_STRING(TYPE_EFIZZER_PROP_SOCKET, EfizzerState, sock_addr),
    DEFINE_PROP_END_OF_LIST(),
};

static void efizzer_class_init(ObjectClass *oc, void *data)
{
    DeviceClass *k = DEVICE_CLASS(oc);

    /* Устанвливаем указатели на функции */
    k->realize = efizzer_realize;
    k->unrealize = efizzer_unrealize;

    /* Устанавливаем свойства */
    device_class_set_props(k, efizzer_properties);
}

// ====================================================================

static void efizzer_register_type(void)
{

    /* Заполняем информацию о созданном нами типе */
    static const TypeInfo efizzer_info = {
      .name           =  TYPE_EFIZZER,
      .parent         =  TYPE_SYS_BUS_DEVICE,
      .instance_size  =  sizeof(EfizzerState),
      .class_init     =  efizzer_class_init,
    };

    /* Регистрируем новый тип в Qemu */
    type_register_static(&efizzer_info);
}

// ====================================================================

/* Устанавливаем конструктор для нового типа */
type_init(efizzer_register_type);

