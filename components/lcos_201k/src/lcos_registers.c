#include "lcos_201k_types.h"

/**
 * @brief Default register configuration for LCoS 201k display
 *
 * These values configure the display for:
 * - 300x224 native resolution (1:1 mapping)
 * - Internal VALID generation (SyncMode=01)
 * - No dither, RGB serial mode
 * - Max brightness, Gamma 2.1
 */

typedef struct {
    uint8_t addr;
    uint8_t value;
    const char *name;
} lcos_reg_entry_t;

const lcos_reg_entry_t lcos_default_regs[] = {
    {0x00, 0x01, "DisplayCfg"},     // Reserved, must be 0x01
    {0x01, 0x20, "VidCfg0"},        // SyncMode=01(Int VALID), NoDither, RGBSerial
    {0x02, 0xC0, "VidCfg1"},        // VPol=Low, HPol=Low, ValidPol=High
    {0x03, 0xC0, "Brightness"},     // Min brightness (0xC0 | 0x00)
    {0x04, 0x00, "VOffset"},        // No interpolation
    {0x05, 0x1C, "HDelay"},         // tHW + tHBP - 2 = 10 + 20 - 2 = 28
    {0x06, 0x00, "VidCfg3"},        // NSleepMode=0 (Sleep), no flip
    {0x07, 0x0A, "ScaleHi"},        // 1:1 Scaling coefficients
    {0x08, 0x00, "HScale"},         // HScaleStep[7:0] = 0
    {0x09, 0x01, "HCycle"},         // HScaleCycle = 1
    {0x0A, 0x00, "VScale"},         // VScaleStep[7:0] = 0
    {0x0B, 0x01, "VCycle"},         // VScaleCycle = 1
    {0x0C, 0x15, "VDelay"},         // tVW + tVBP = 3 + 18 = 21
    {0x0D, 0x00, "Cinema"},         // No cinema mode
    {0x0E, 0x09, "Gamma"},          // Gamma 2.1
};

const size_t lcos_default_regs_count = sizeof(lcos_default_regs) / sizeof(lcos_default_regs[0]);

// Register addresses
#define LCOS_REG_DISP_CFG       0x00
#define LCOS_REG_VID_CFG0       0x01
#define LCOS_REG_VID_CFG1       0x02
#define LCOS_REG_BRIGHTNESS     0x03
#define LCOS_REG_V_OFFSET       0x04
#define LCOS_REG_H_DELAY        0x05
#define LCOS_REG_VID_CFG3       0x06
#define LCOS_REG_SCALE_HI       0x07
#define LCOS_REG_H_SCALE        0x08
#define LCOS_REG_H_CYCLE        0x09
#define LCOS_REG_V_SCALE        0x0A
#define LCOS_REG_V_CYCLE        0x0B
#define LCOS_REG_V_DELAY        0x0C
#define LCOS_REG_CINEMA         0x0D
#define LCOS_REG_GAMMA          0x0E
#define LCOS_REG_HW_CFG         0x18

// VID_CFG3 (0x06) bits
#define LCOS_VID_CFG3_VFLIP     (1 << 6)
#define LCOS_VID_CFG3_HFLIP     (1 << 5)
#define LCOS_VID_CFG3_NSLEEP    (1 << 3)

// SPI command masks
#define LCOS_CMD_WRITE          0x00
#define LCOS_CMD_READ           0x80

// Expected hardware ID
#define LCOS_HW_ID_EXPECTED     0x54
