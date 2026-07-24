#include "views.h"
#include "common/dpy.h"
#include "common/sfp.h"
#include "common/sfp_hw.h"
#include "common/view_util.h"
#include "common/util.h"
#include <stddef.h>

/* Reclassifies a 10G passive DAC as a 25G-capable module (or back again) by
 * rewriting a few SFF-8472 identification bytes and their page checksums,
 * per the well-known "unlock25" EEPROM trick. Direction is auto-detected
 * from the module's current rate byte. Not all cables are electrically
 * capable of 25G, and reverting restores standard 10G defaults rather than
 * this specific cable's original bytes (Hubble doesn't persist a backup
 * across sessions), so this requires an explicit arm (SELECT) + confirm
 * (long-press SELECT).
 *
 * The five target bytes are written one at a time (the interface has no
 * transactional multi-byte write), stopping at the first failure. Whatever
 * bytes already changed are then written back to their original values, so
 * a mid-sequence failure doesn't leave the module with a checksum that
 * matches neither the old nor the new identity. The revert writes go over
 * the same bus and can fail too, so there are three possible outcomes, not
 * two. */

typedef struct {
    uint8_t baudrate;
    uint8_t transceiver2;
    uint8_t upper_bit_rate_margin;
} rate_profile_t;

static const rate_profile_t PROFILE_25G = {0xff, 0x0d, 0x68};
static const rate_profile_t PROFILE_10G = {0x67, 0x00, 0x00};

static uint8_t is_25g(const sfp_sid_t *sid)
{
    return sid->baudrate == 0xff;
}

typedef enum {
    RESULT_NONE,
    RESULT_OK,
    RESULT_REVERTED,
    RESULT_INCONSISTENT,
} write_result_t;

static uint8_t armed = 0;
static uint8_t result = RESULT_NONE;

static uint8_t write_byte(uint8_t offset, uint8_t value)
{
    return sfp_hw_write(SFP_I2C_ADDR_SID, offset, 1, &value) == SFP_READ_OK;
}

static const size_t rewrite_offsets[] = {
    offsetof(sfp_sid_t, baudrate),              /* 0x0C */
    offsetof(sfp_sid_t, transceiver2),          /* 0x24 */
    offsetof(sfp_sid_t, upper_bit_rate_margin), /* 0x42 */
    offsetof(sfp_sid_t, cc_base),               /* 0x3F */
    offsetof(sfp_sid_t, cc_ext),                /* 0x5F */
};
#define N_REWRITE_BYTES ARRAY_SIZE(rewrite_offsets)

static write_result_t do_rewrite(void)
{
    if (!sfp_hw_try_lock()) {
        return RESULT_NONE; /* couldn't even attempt; nothing touched */
    }

    sfp_sid_t orig = *sfp_sid_get();
    const rate_profile_t *target = is_25g(&orig) ? &PROFILE_10G : &PROFILE_25G;

    sfp_sid_t sid = orig;
    sid.baudrate = target->baudrate;
    sid.transceiver2 = target->transceiver2;
    sid.upper_bit_rate_margin = target->upper_bit_rate_margin;
    sid.cc_base = sfp_cc_base_compute(&sid);
    sid.cc_ext = sfp_cc_ext_compute(&sid);

    const uint8_t *orig_bytes = (const uint8_t *)&orig;
    const uint8_t *new_bytes = (const uint8_t *)&sid;

    uint8_t written = 0;
    for (; written < N_REWRITE_BYTES; written++) {
        size_t off = rewrite_offsets[written];
        if (!write_byte(off, new_bytes[off])) {
            break;
        }
    }

    write_result_t write_result;
    if (written == N_REWRITE_BYTES) {
        write_result = RESULT_OK;
    }
    else {
        uint8_t reverted_ok = 1;
        for (uint8_t i = 0; i < written; i++) {
            size_t off = rewrite_offsets[i];
            if (!write_byte(off, orig_bytes[off])) {
                reverted_ok = 0;
            }
        }
        write_result = reverted_ok ? RESULT_REVERTED : RESULT_INCONSISTENT;
    }

    sfp_hw_read(SFP_I2C_ADDR_SID, 0, sizeof(sfp_sid_t), sfp_sid_get());
    sfp_hw_unlock();
    return write_result;
}

void view_25g_main(const event_t *event)
{
    dpy_clear();
    view_draw_header("25G");

    if (event->type == EVENT_LEAVE) {
        armed = 0;
        result = RESULT_NONE;
        return;
    }

    if (sfp_state_get() != SFP_STATE_READY) {
        armed = 0;
        dpy_set_font(DPY_FONT_16_BOLD);
        dpy_puts(8, 24, "no transceiver");
        return;
    }

    if (event->type == EVENT_BUTTON) {
        if (!armed) {
            if (event->param == EVENT_BUTTON_SELECT) {
                armed = 1;
                result = RESULT_NONE;
            }
        }
        else if (event->param == (EVENT_BUTTON_SELECT | EVENT_BUTTON_LONG)) {
            result = do_rewrite();
            armed = 0;
        }
        else if (event->param != EVENT_BUTTON_NONE) {
            armed = 0; /* any other button cancels */
        }
    }

    const sfp_sid_t *sid = sfp_sid_get();
    uint8_t currently_25g = is_25g(sid);
    dpy_puts(0, 8, "Rate byte 0x0C:");
    dpy_putix(90, 8, 2, sid->baudrate);
    dpy_puts(0, 16, currently_25g ? "25G-class" : "10G-class DAC");

    if (!armed) {
        dpy_puts(0, 32, currently_25g ? "SELECT: arm to 10G" : "SELECT: arm to 25G");
    }
    else {
        dpy_puts(0, 32, currently_25g ? "LONG SEL = to 10G!" : "LONG SEL = to 25G!");
        dpy_puts(0, 40, "success = permanent");
        dpy_puts(0, 48, "other button: cancel");
    }

    if (result == RESULT_OK) {
        dpy_puts(0, 56, currently_25g ? "OK - now 25G" : "OK - now 10G");
    }
    else if (result == RESULT_REVERTED) {
        dpy_puts(0, 56, "failed, reverted OK");
    }
    else if (result == RESULT_INCONSISTENT) {
        dpy_puts(0, 56, "INCONSISTENT! check");
    }
}
