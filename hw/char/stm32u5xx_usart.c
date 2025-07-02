/*
 * STM32U5XX USART
 *
 * Copyright (c) 2025 Miikka Lukumies
 * Copyright (c) 2014 Alistair Francis <alistair@alistair23.me>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "hw/char/stm32u5xx_usart.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"

#include "trace.h"

static int stm32u5xx_usart_can_receive(void *opaque)
{
    STM32U5XXUsartState *s = opaque;

    if (!(s->usart_isr & USART_ISR_RXNE)) {
        return 1;
    }

    return 0;
}

static void stm32u5xx_update_irq(STM32U5XXUsartState *s)
{
    uint32_t mask = s->usart_isr & s->usart_cr1;

    /* Generate an interrupt when either the
     * - Transmit data register empty (TXE)
     * - Transmit complete (TC)
     * - Read data register not empty (RXNE)
     * flags are set (ISR) and enabled (CR1)
     */
    if (mask & (USART_ISR_TXE | USART_ISR_TC | USART_ISR_RXNE)) {
        qemu_set_irq(s->irq, 1);
    } else {
        qemu_set_irq(s->irq, 0);
    }
}

static void stm32u5xx_usart_receive(void *opaque, const uint8_t *buf, int size)
{
    STM32U5XXUsartState *s = opaque;

    if (!(s->usart_cr1 & USART_CR1_UE && s->usart_cr1 & USART_CR1_RE)) {
        /* USART not enabled - drop the chars */
        return;
    }

    /* Copy byte to receive data register */
    s->usart_rdr = *buf;
    /* Set the Read data register not empty flag/interrupt*/
    s->usart_isr |= USART_ISR_RXNE;

    stm32u5xx_update_irq(s);
}

static void stm32u5xx_usart_reset(DeviceState *dev)
{
    STM32U5XXUsartState *s = STM32U5XX_USART(dev);

    /* Set register reset values */
    s->usart_cr1 = 0x00000000;
    s->usart_cr2 = 0x00000000;
    s->usart_cr3 = 0x00000000;
    s->usart_brr = 0x00000000;
    s->usart_gtpr = 0x00000000;
    s->usart_rtor = 0x00000000;
    s->usart_rqr = 0x00000000;
    s->usart_icr = 0x00000000;
    s->usart_rdr = 0x00000000;
    s->usart_tdr = 0x00000000;
    s->usart_presc = 0x00000000;
    s->usart_autocr = 0x00000000;

    /* In synchronous mode, this flag cannot be unset, TX data register always empty */
    s->usart_isr = USART_ISR_TXE; 

    stm32u5xx_update_irq(s);
}

static uint64_t stm32u5xx_usart_read(void *opaque, hwaddr addr,
                                       unsigned int size)
{
    STM32U5XXUsartState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case USART_ISR:
        retvalue = s->usart_isr;
        qemu_chr_fe_accept_input(&s->chr);
        break;
    case USART_RDR:
        /* Read from data register, clear RX data register Non-Empty flag */
        retvalue = s->usart_rdr & 0x3FF;
        s->usart_isr &= ~USART_ISR_RXNE;
        qemu_chr_fe_accept_input(&s->chr);
        stm32u5xx_update_irq(s);
        break;
    case USART_BRR:
        retvalue = s->usart_brr;
        break;
    case USART_CR1:
        retvalue = s->usart_cr1;
        break;
    case USART_CR2:
        retvalue = s->usart_cr2;
        break;
    case USART_CR3:
        retvalue = s->usart_cr3;
        break;
    case USART_GTPR:
        retvalue = s->usart_gtpr;
        break;
    case USART_RTOR:
        retvalue = s->usart_rtor;
        break;
    case USART_RQR:
        retvalue = s->usart_rqr;
        break;
    case USART_ICR:
        retvalue = 0x0; /* write-only */
        break;
    case USART_TDR:
        retvalue = s->usart_tdr;
        break;
    case USART_PRESC:
        retvalue = s->usart_presc;
        break;
    case USART_AUTOCR:
        retvalue = s->usart_autocr;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        return 0;
    }

    return retvalue;
}

static void stm32u5xx_usart_write(void *opaque, hwaddr addr,
                                  uint64_t val64, unsigned int size)
{
    STM32U5XXUsartState *s = opaque;
    uint32_t value = val64;
    unsigned char ch;

    switch (addr) {
    case USART_ISR:
        /* This register is read-only, don't do anything */
        return;
    case USART_ICR:
        /* Ignore bits that are reserved or set by HW */
        uint32_t ignore_mask = 0xFDF5C4A0;

        /* Clear the no-touch bits */
        value &= ~(ignore_mask);

        /* Clear the remaining bits from ISR */
        s->usart_isr &= ~(value);

        stm32u5xx_update_irq(s);
        return;
    case USART_TDR:
        if (value < 0x100) {
            ch = value;
            /* XXX this blocks entire thread. Rewrite to use
             * qemu_chr_fe_write and background I/O callbacks */
            qemu_chr_fe_write_all(&s->chr, &ch, 1);
            /* XXX I/O are currently synchronous, making it impossible for
               software to observe transient states where TXE or TC aren't
               set. Unlike TXE however, which is read-only, software may
               clear TC by writing 0 to the SR register, so set it again
               on each write. */
            s->usart_isr |= USART_ISR_TC;
            stm32u5xx_update_irq(s);
        }
        return;
    case USART_RDR:
        /* This register is read-only, don't do anything */
        return;
    case USART_BRR:
        s->usart_brr = value;
        return;
    case USART_CR1:
        s->usart_cr1 = value;
        stm32u5xx_update_irq(s);
        return;
    case USART_CR2:
        s->usart_cr2 = value;
        return;
    case USART_CR3:
        s->usart_cr3 = value;
        return;
    case USART_GTPR:
        s->usart_gtpr = value;
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
    }
}

static const MemoryRegionOps stm32u5xx_usart_ops = {
    .read = stm32u5xx_usart_read,
    .write = stm32u5xx_usart_write,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static const Property stm32u5xx_usart_properties[] = {
    DEFINE_PROP_CHR("chardev", STM32U5XXUsartState, chr),
};

static void stm32u5xx_usart_init(Object *obj)
{
    STM32U5XXUsartState *s = STM32U5XX_USART(obj);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &stm32u5xx_usart_ops, s,
                          TYPE_STM32U5XX_USART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void stm32u5xx_usart_realize(DeviceState *dev, Error **errp)
{
    STM32U5XXUsartState *s = STM32U5XX_USART(dev);

    qemu_chr_fe_set_handlers(&s->chr, stm32u5xx_usart_can_receive,
                             stm32u5xx_usart_receive, NULL, NULL,
                             s, NULL, true);
}

static void stm32u5xx_usart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    device_class_set_legacy_reset(dc, stm32u5xx_usart_reset);
    device_class_set_props(dc, stm32u5xx_usart_properties);
    dc->realize = stm32u5xx_usart_realize;
}

static const TypeInfo stm32u5xx_usart_info = {
    .name          = TYPE_STM32U5XX_USART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(STM32U5XXUsartState),
    .instance_init = stm32u5xx_usart_init,
    .class_init    = stm32u5xx_usart_class_init,
};

static void stm32u5xx_usart_register_types(void)
{
    type_register_static(&stm32u5xx_usart_info);
}

type_init(stm32u5xx_usart_register_types)
