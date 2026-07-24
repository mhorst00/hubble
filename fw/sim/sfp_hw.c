#include "common/sfp_hw.h"
#include "common/sfp.h"
#include <string.h>

/* Fake 10G DAC EEPROM image (SFF-8472 base ID page, I2C addr 0x50) so the
 * simulator has something realistic to read/write without real hardware. */
static uint8_t fake_eeprom[sizeof(sfp_sid_t)];

static void pad_copy(void *dst, const char *src, uint8_t n)
{
    memset(dst, ' ', n);
    uint8_t l = strlen(src);
    memcpy(dst, src, l < n ? l : n);
}

static void fake_eeprom_init(void) __attribute__((constructor));

static void fake_eeprom_init(void)
{
    sfp_sid_t sid;
    memset(&sid, 0, sizeof(sid));
    sid.id = 0x03;         /* SFP/SFP+ */
    sid.ext_id = 0x04;     /* GBIC/SFP defined by 2-wire interface ID */
    sid.connector = 0x21;  /* Copper pigtail (typical DAC) */
    sid.transceiver[0] = 1 << 3; /* SFF-8472: passive cable, per Table 3.5 byte 8 bit 3 */
    sid.encoding = 0x06;   /* 64B/66B */
    sid.baudrate = 0x67;   /* 103 * 100 MBd = 10.3 Gbd */
    sid.rate = 0;
    sid.length_copper = 1; /* 1 m */
    pad_copy(sid.vendor_name, "SWABIAN", sizeof(sid.vendor_name));
    pad_copy(sid.vendor_pn, "TEST-DAC10G", sizeof(sid.vendor_pn));
    pad_copy((char *)sid.vendor_rev, "A1", sizeof(sid.vendor_rev));
    pad_copy(sid.vendor_sn, "SIM0000000000001", sizeof(sid.vendor_sn));
    memcpy(sid.date_code, "26072400", sizeof(sid.date_code));
    sid.cc_base = sfp_cc_base_compute(&sid);
    sid.cc_ext = sfp_cc_ext_compute(&sid);

    memcpy(fake_eeprom, &sid, sizeof(fake_eeprom));

    memcpy(sfp_sid_get(), &sid, sizeof(sid));
    sfp_state_set(SFP_STATE_READY);
}

uint8_t sfp_hw_try_lock(void)
{
    return 1;
}
void sfp_hw_unlock(void)
{
}
void sfp_hw_req(uint8_t op)
{
}

uint8_t sfp_hw_read(uint8_t i2c_addr, uint8_t mem_addr, uint8_t len, void *dest)
{
    if (i2c_addr == SFP_I2C_ADDR_SID && mem_addr + (uint16_t)len <= sizeof(fake_eeprom)) {
        memcpy(dest, fake_eeprom + mem_addr, len);
    }
    else if (i2c_addr == SFP_I2C_ADDR_MON) {
        memset(dest, 0, len);
    }
    return SFP_READ_OK;
}

uint8_t sfp_hw_write(uint8_t i2c_addr, uint8_t mem_addr, uint8_t len, const void *src)
{
    if (i2c_addr == SFP_I2C_ADDR_SID && mem_addr + (uint16_t)len <= sizeof(fake_eeprom)) {
        memcpy(fake_eeprom + mem_addr, src, len);
    }
    return SFP_READ_OK;
}

static uint8_t tx_dis = 0;
void sfp_hw_set_tx_dis(uint8_t dis) {
	tx_dis = dis;
}
uint8_t sfp_hw_get_tx_dis(void){
	return tx_dis;
}

static uint8_t auto_on = 1;
void sfp_hw_set_auto_on(uint8_t v)
{
    auto_on = v;
}
uint8_t sfp_hw_get_auto_on(void)
{
    return auto_on;
}

uint8_t sfp_hw_read_status(void)
{
    return 0;
}
