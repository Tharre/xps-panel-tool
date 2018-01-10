#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/limits.h>
#include <linux/i2c.h>
#include <linux/i2c-dev.h>

enum tcon_kind {
  tcNT71897_24C32,
  tcNT71394,
  tcNT71897NB,
  tcNT71395,
};

struct tcon_config {
  char            caption[64];
  enum tcon_kind  tcon_kind;
  uint8_t         tcon_addr;
  size_t          eeprom_size;
  size_t          eeprom_rd_buf_size;
  size_t          eeprom_wr_buf_size;
  uint16_t        major_ver_addr;
  uint16_t        minor_ver_addr;
  uint16_t        vcom_addr;
};

static int parse_config(const char *path, struct tcon_config *cfg) {
  FILE *f = fopen(path, "rb");
  if(!f) {
    fprintf(stderr, "Cannot open %s\n", path);
    return 0;
  }

  printf("Parsing configuration %s\n", path);

  enum { sBEGIN, sCOMMENT, sSECTION, sKEY, sEQ, sVALUE } state;
  char section[64], key[64], value[64];
  int psection, pkey, pvalue;
  int key_at;

  state = sBEGIN;
  while(1) {
    char c = fgetc(f);
    if(c == '\r') continue;
    if(c == EOF) break;

    switch(state) {
    case sBEGIN:
      if(c == ' ' || c == '\n') {
      } else if(c == ';') {
        state = sCOMMENT;
      } else if(c == '[') {
        state = sSECTION;
        psection = 0;
        memset(section, 0, sizeof(section));
      } else if(isalnum(c)) {
        state = sKEY;
        pkey = 0;
        memset(key, 0, sizeof(key));
        ungetc(c, f);
        key_at = ftell(f);
      } else goto fail;
      continue;

    case sCOMMENT:
      if(c == '\n') {
        state = sBEGIN;
      }
      continue;

    case sSECTION:
      if(isalnum(c) || c == '_') {
        section[psection++] = c;
      } else if(c == ']') {
        state = sCOMMENT;
        section[psection] = 0;
        // printf("S: %s\n", section);
      } else goto fail;
      continue;

    case sKEY:
      if(c == ' ') {
      } else if(isalnum(c) || c == '_') {
        key[pkey++] = c;
      } else if(c == '=') {
        state = sEQ;
        key[pkey] = 0;
        // printf("K: %s\n", key);
      } else goto fail;
      continue;

    case sEQ:
      if(c == ' ') {
      } else {
        state = sVALUE;
        pvalue = 0;
        memset(value, 0, sizeof(value));
        ungetc(c, f);
      }
      continue;

    case sVALUE:
      if(c != '\n') {
        value[pvalue++] = c;
      } else {
        state = sBEGIN;
        // printf("V: %s\n", value);

        if(!strcmp(key, "APP_CAPTION")) {
          strcpy(cfg->caption, value);
        } else if(!strcmp(key, "TCON_INIT_FLOW")) {
          switch(strtol(value, NULL, 0)) {
          case 0: cfg->tcon_kind = tcNT71897_24C32; break;
          case 1: cfg->tcon_kind = tcNT71394; break;
          case 2: cfg->tcon_kind = tcNT71897NB; break;
          case 3: cfg->tcon_kind = tcNT71395; break;
          default:
            fprintf(stderr, "Unknown TCON_INIT_FLOW value %s\n", value);
            fseek(f, key_at, SEEK_SET);
            goto fail;
          }
        } else if(!strcmp(key, "TCON_ADDR")) {
          cfg->tcon_addr = strtol(value, NULL, 0) >> 1;
        } else if(!strcmp(key, "EEPROM_SIZE")) {
          cfg->eeprom_size = strtol(value, NULL, 0);
        } else if(!strcmp(key, "EEPROM_RD_BUF_SIZE")) {
          cfg->eeprom_rd_buf_size = strtol(value, NULL, 0);
        } else if(!strcmp(key, "EEPROM_WR_BUF_SIZE")) {
          cfg->eeprom_wr_buf_size = strtol(value, NULL, 0);
        } else if(!strcmp(key, "MAJOR_VER_ADDR")) {
          cfg->major_ver_addr = strtol(value, NULL, 0);
        } else if(!strcmp(key, "MINOR_VER_ADDR")) {
          cfg->minor_ver_addr = strtol(value, NULL, 0);
        } else if(!strcmp(key, "VCOM_ADDR")) {
          cfg->vcom_addr = strtol(value, NULL, 0);
        }
      }
      continue;
    }
  }

  const char *tcon_kind_name;
  switch(cfg->tcon_kind) {
    case tcNT71897_24C32: tcon_kind_name = "NT71897_24C32"; break;
    case tcNT71394:       tcon_kind_name = "NT71394"; break;
    case tcNT71897NB:     tcon_kind_name = "NT71897NB"; break;
    case tcNT71395:       tcon_kind_name = "NT71395"; break;
  }

  printf(
    "Configuration for %s:\n"
    "  TCON access flow:      %s\n"
    "  TCON address:          %02x\n"
    "  EEPROM size:           %04x\n"
    "  EEPROM read buf size:  %04x\n"
    "  EEPROM write buf size: %04x\n"
    "  major version offset:  %04x\n"
    "  minor version offset:  %04x\n"
    "  Vcom offset:           %04x\n\n",
    cfg->caption,
    tcon_kind_name, cfg->tcon_addr,
    cfg->eeprom_size, cfg->eeprom_rd_buf_size, cfg->eeprom_wr_buf_size,
    cfg->major_ver_addr, cfg->minor_ver_addr, cfg->vcom_addr);

  fclose(f);
  return 1;

fail: ;
  char context[64];
  memset(context, 0, sizeof(context));

  fseek(f, -1, SEEK_SET);
  fgets(context, sizeof(context), f);
  fprintf(stderr, "Cannot parse config at \"%s\"...\n", context);

  fclose(f);
  return 0;
}

static int open_i2c(const char *path) {
  int i2cfd;
  if((i2cfd = open(path, O_RDWR)) < 0) {
    fprintf(stderr, "Cannot open %s\n", path);
    if(errno == EACCES)
      fprintf(stderr, "Permission denied, are you root?\n");
    return -1;
  }

  unsigned long funcs;
  if(ioctl(i2cfd, I2C_FUNCS, &funcs) < 0) {
    fprintf(stderr, "Cannot query adapter functionality\n");
    return -1;
  }

  if(!(funcs & I2C_FUNC_I2C)) {
    fprintf(stderr, "I2C adapter does not support true I2C\n");
    return -1;
  }

  return i2cfd;
}

static int read_i2c(int i2cfd, uint8_t addr, int offset_16bit, uint16_t offset,
                    size_t length, uint8_t *data) {
  uint8_t offset_buf_8bit[] = { offset };
  uint8_t offset_buf_16bit[] = { offset >> 8, offset };
  struct i2c_msg msgs[] = {
    {
      .addr  = addr,
      .flags = 0,
      .len   = offset_16bit ? 2 : 1,
      .buf   = offset_16bit ? offset_buf_16bit : offset_buf_8bit,
    },
    {
      .addr  = addr,
      .flags = I2C_M_RD,
      .len   = length,
      .buf   = data
    }
  };
  struct i2c_rdwr_ioctl_data rdwr = {
    .msgs  = msgs,
    .nmsgs = sizeof(msgs) / sizeof(msgs[0])
  };

  if(ioctl(i2cfd, I2C_RDWR, &rdwr) < 0) {
    fprintf(stderr, "Cannot perform I2C device %02x read of %04x:%04x\n", addr, offset, length);
    return 0;
  }

  return 1;
}

static int read_i2c_8bit(int i2cfd, uint8_t addr, uint16_t offset,
                         size_t length, uint8_t *data) {
  read_i2c(i2cfd, addr, 0, offset, length, data);
}


static int read_i2c_16bit(int i2cfd, uint8_t addr, uint16_t offset,
                         size_t length, uint8_t *data) {
  read_i2c(i2cfd, addr, 1, offset, length, data);
}

static int verify_edid(int i2cfd, uint16_t *panelId, char *panelCode) {
  uint8_t edid[128];
  if(!read_i2c_8bit(i2cfd, 0x50, 0, sizeof(edid), edid)) {
    fprintf(stderr, "Cannot read EDID\n");
    return 0;
  }

  if(strcmp(&edid[0], "\x00\xff\xff\xff\xff\xff\xff\x00")) {
    fprintf(stderr, "Corrupted EDID data\n");
    return 0;
  }

  if(!(edid[8] == 0x4d && edid[9] == 0x10) /*SHP*/) {
    fprintf(stderr, "Panel vendor is not Sharp\n");
    return 0;
  }
  *panelId = (edid[11] << 8) | edid[10];

  memset(panelCode, 0, 6);
  for(int i = 0; i < 4; i++) {
    uint8_t *desc = &edid[0x36 + 0x12 * i];
    if(!(desc[0] == 0 && desc[1] == 0 && desc[3] == 0xfe)) {
      continue;
    }
    memcpy(panelCode, &desc[5], 5);
  }
  if(panelCode[0] == 0) {
    fprintf(stderr, "Panel metadata missing\n");
    return 0;
  }

  printf("Panel EDID indicates SHP %04x %s\n\n", *panelId, panelCode);

  return 1;
}

/*
static int read_tcon(int i2cfd, struct tcon_config *cfg, uint16_t offset,
                     size_t length, uint8_t *data) {
  if(cfg->tcon_kind == tcNT71394) {
    // Instead of a single large EEPROM at address A, we get many small
    // (EEPROM_WR_BUF_SIZE long) EEPROMs at addresses A, A+1, A+2...
    uint8_t addr = cfg->tcon_addr + offset / cfg->eeprom_wr_buf_size;
    offset %= cfg->eeprom_wr_buf_size;
    return read_i2c(i2cfd, addr, 0, offset, length, data);
  } else {
    fprintf(stderr, "Unsupported TCON access flow\n");
    return 1;
  }
}

static int ident_panel_firmware(int i2cfd, struct tcon_config *cfg) {
  uint8_t major_ver, minor_ver, vcom;

  if(!read_tcon(i2cfd, cfg, cfg->major_ver_addr, 1, &major_ver) ||
     !read_tcon(i2cfd, cfg, cfg->minor_ver_addr, 1, &minor_ver) ||
     !read_tcon(i2cfd, cfg, cfg->vcom_addr, 1, &vcom)) {
    fprintf(stderr, "Cannot identify panel firmware\n");
    return 0;
  }

  printf(
    "Current panel firmware:\n"
    "  version %x.%02x\n"
    "  Vcom %02x\n\n",
    major_ver, minor_ver, vcom);
  return 1;
}
*/

static int read_firmware(int i2cfd, struct tcon_config *cfg, const char *path) {
  uint8_t *firmware = calloc(cfg->eeprom_size, 1);
  if(!firmware) abort();

  FILE *f = fopen(path, "wb");
  if(!f) {
    fprintf(stderr, "Cannot open %s\n", path);
    return 0;
  }

  printf("Reading firmware ");
  uint8_t offset = 0;
  struct i2c_msg msgs[] = {
    {
      .addr  = cfg->tcon_addr,
      .flags = 0,
      .len   = 1,
      .buf   = &offset,
    },
    {
      .addr  = cfg->tcon_addr,
      .flags = I2C_M_RD,
      .len   = 256,
      .buf   = &firmware[0]
    },
    {
      .addr  = cfg->tcon_addr,
      .flags = I2C_M_RD,
      .len   = 256,
      .buf   = &firmware[256]
    }
  };
  struct i2c_rdwr_ioctl_data rdwr = {
    .msgs  = msgs,
    .nmsgs = sizeof(msgs) / sizeof(msgs[0])
  };

  if(ioctl(i2cfd, I2C_RDWR, &rdwr) < 0) {
    fprintf(stderr, "Cannot perform xxx\n");
    return 0;
  }

  // read_i2c_8bit(i2cfd, cfg->tcon_addr, 128, 256, &firmware[0]);
  // read_i2c_8bit(i2cfd, cfg->tcon_addr + 1, 0, 128, &firmware[256]);

  // const uint16_t step = 256;
  // for(uint16_t offset = 0; offset < cfg->eeprom_size; offset += step) {
  //   size_t length = offset + step > cfg->eeprom_size ? cfg->eeprom_size - offset : step;
  //   if(!read_tcon(i2cfd, cfg, offset, length, &firmware[offset])) {
  //     fprintf(stderr, "Cannot read firmware\n");
  //     return 0;
  //   } else {
  //     printf(".");
  //     fflush(stdout);
  //   }
  // }
  printf("\n");

  if(fwrite(firmware, 1, cfg->eeprom_size, f) != cfg->eeprom_size) {
    fprintf(stderr, "Cannot write %s\n", path);
    return 0;
  }

  fclose(f);
  return 1;
}

static void help() {
  fprintf(stderr,
    "Usage: xps13-panel-tool COMMAND\n"
    "  COMMAND can be one of: ident, read, write;\n"
    "    * probe - read the EDID and check if panel is supported\n"
    "    * ident - read the panel firmware version\n"
    "    * backup PATH - back up the firmware from the panel to PATH\n"
    "    * flash VARIANT - write the firmware VARIANT to the panel\n\n");
}

int main(int argc, char **argv) {
  char path[PATH_MAX];

  if(argc < 2) {
    help();
    return 1;
  }

  enum { cPROBE, cIDENT, cBACKUP, cFLASH } command;
       if(!strcmp(argv[1], "probe"))  command = cPROBE;
  else if(!strcmp(argv[1], "ident"))  command = cIDENT;
  else if(!strcmp(argv[1], "backup")) command = cBACKUP;
  else if(!strcmp(argv[1], "flash"))  command = cFLASH;
  else {
    help();
    return 1;
  }

  if((command == cPROBE || command == cIDENT) && argc != 2 ||
     (command == cBACKUP || command == cFLASH) && argc != 3) {
    help();
    return 1;
  }

  int i2cfd;
  if((i2cfd = open_i2c("/dev/i2c-4")) < 0) return 1;

  uint16_t panelId;
  char panelCode[6];
  if(!verify_edid(i2cfd, &panelId, panelCode)) return 1;

  if(command == cPROBE) return 0;

  strcpy(path, "firmware/");
  strcat(path, panelCode);
  strcat(path, "/model.ini");
  struct tcon_config cfg;
  if(!parse_config(path, &cfg)) return 1;

  if(!getenv("XPS_PANEL_TOOL_NONINTERACTIVE")) {
    printf(
      "               /!\\ WARNING WARNING WARNING /!\\\n"
      "XPS-PANEL-TOOL IS THIRD-PARTY SOFTWARE PROVIDED WITH ABSOLUTELY\n"
      "NO WARRANTY, EXPRESS OR IMPLIED, INCLUDING THE WARRANTY OF NOT TEMPORARILY\n"
      "OR PERMANENTLY MAKING YOUR HARDWARE UNUSABLE. USE AT YOUR OWN RISK.\n"
      "\n"
      "Do you want to proceed? Type \"I understand\" to continue: ");
    char prompt[64] = {0};
    fgets(prompt, sizeof(prompt), stdin);
    if(strcmp(prompt, "I understand\n")) {
      printf("Exiting.\n");
      return 0;
    } else {
      printf("\n");
    }
  }

  // if(!ident_panel_firmware(i2cfd, &cfg)) return 1;

  if(command == cIDENT) return 0;

  if(command == cBACKUP) {
    if(!read_firmware(i2cfd, &cfg, argv[2])) return 1;
  }

  return 0;
}
