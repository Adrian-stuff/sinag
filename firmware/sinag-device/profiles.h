#pragma once
// =============================================================================
//  Brand profiles — map an inverter's own Modbus registers to Sinag's fields.
//
//  Every profile fills the same 8 fields (Contract A / Contract B). For each
//  field give: the register address, a scale (value = raw * scale), and whether
//  the register is signed int16. reg = -1 means "this inverter doesn't have it".
//
//  To add a real inverter later: copy PROFILE_SINAG_SIM, change the numbers
//  from the brand's Modbus manual, and point PROFILE in config.h at it.
// =============================================================================
#include <stdint.h>

enum Field : uint8_t { F_PV_W, F_LOAD_W, F_GRID_W, F_BATT_SOC, F_BATT_W, F_GRID_ON, F_INV_TEMP_C, F_FAULT_CODE, F_COUNT };

static const char *const FIELD_NAMES[F_COUNT] = {
  "pv_w", "load_w", "grid_w", "batt_soc", "batt_w", "grid_on", "inv_temp_c", "fault_code"
};

struct FieldMap {
  int32_t reg;        // register address, -1 = not available
  float   scale;      // engineering value = raw * scale
  bool    isSigned;   // raw is int16 two's complement
};

struct BrandProfile {
  const char *name;
  uint8_t     slaveId;
  uint32_t    baud;
  uint8_t     function;          // 3 = holding registers, 4 = input registers
  float       gridOnThreshold;   // grid_on = value > threshold (works for a flag or a grid-voltage register)
  FieldMap    f[F_COUNT];
};

// Sinag inverter simulator — Contract A, 1:1.
static const BrandProfile PROFILE_SINAG_SIM = {
  "sinag-sim", 1, 9600, 3, 0.5f,
  {
    /* pv_w       */ { 0, 1.0f, false },
    /* load_w     */ { 1, 1.0f, false },
    /* grid_w     */ { 2, 1.0f, true  },
    /* batt_soc   */ { 3, 1.0f, false },
    /* batt_w     */ { 4, 1.0f, true  },
    /* grid_on    */ { 5, 1.0f, false },
    /* inv_temp_c */ { 6, 1.0f, true  },
    /* fault_code */ { 7, 1.0f, false },
  }
};

// =============================================================================
//  Smart meter profiles (float32 registers, two per value).
//  Fields are published on sinag/{device_id}/meter.
// =============================================================================
enum MeterField : uint8_t { M_VOLTAGE, M_CURRENT, M_POWER, M_APPARENT, M_REACTIVE, M_PF, M_FREQ, M_IMPORT, M_EXPORT, M_COUNT };

static const char *const METER_FIELD_NAMES[M_COUNT] = {
  "voltage_v", "current_a", "power_w", "apparent_va", "reactive_var", "pf", "freq_hz", "import_kwh", "export_kwh"
};
static const uint8_t METER_FIELD_DECIMALS[M_COUNT] = { 1, 2, 0, 0, 0, 2, 2, 2, 2 };

struct MeterProfile {
  const char *name;
  uint8_t     slaveId;
  uint32_t    baud;
  uint8_t     function;          // 4 = input registers (SDM measurements)
  bool        wordSwap;          // false = high word first (Eastron), true = low word first
  int32_t     reg[M_COUNT];      // first register of each float32, -1 = not available
};

// Eastron SDM120 (also what the Sinag simulator emulates in METER mode).
// Real SDM120s ship at slave 1 / 2400 baud: set them to slave 2 / 9600 on the meter's menu.
static const MeterProfile METER_SDM120 = {
  "sdm120", 2, 9600, 4, false,
  { 0x0000, 0x0006, 0x000C, 0x0012, 0x0018, 0x001E, 0x0046, 0x0048, 0x004A }
};

// Example of what a real brand looks like (addresses are placeholders — fill in
// from the manual before using). Grid-on is read from a grid-voltage register
// with 0.1 V scale: anything above 150 V counts as "grid available".
static const BrandProfile PROFILE_EXAMPLE_BRAND = {
  "example-brand", 1, 9600, 3, 150.0f,
  {
    /* pv_w       */ { 186, 1.0f, false },
    /* load_w     */ { 178, 1.0f, false },
    /* grid_w     */ { 169, 1.0f, true  },
    /* batt_soc   */ { 184, 1.0f, false },
    /* batt_w     */ { 190, 1.0f, true  },
    /* grid_on    */ { 150, 0.1f, false },
    /* inv_temp_c */ {  90, 0.1f, true  },
    /* fault_code */ { 103, 1.0f, false },
  }
};
