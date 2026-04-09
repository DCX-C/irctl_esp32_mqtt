#ifndef _IR_ENCODER_C_
#define _IR_ENCODER_C_


#define AC_ID_USED AC_PID_GREE

enum AC_PIDS{
    AC_PID_TCL,
    AC_PID_GREE,
    ACDEV_MAX,
};

struct ac_cfg {
    uint8_t open;
    uint8_t temp_act;
    uint8_t temp_slp;
    uint8_t temp;
    uint8_t sleep;
};

struct ac_dev {
    struct ac_cfg cfg;
    struct ac_ops *ops;
    SemaphoreHandle_t mutex;
};

struct ac_ops {
    void (*open)(struct ac_dev *ac);
    void (*close)(struct ac_dev *ac);
    void (*read_cfg)(struct ac_dev *ac, struct ac_cfg *cfg);
    int  (*write_cfg)(struct ac_dev *ac, struct ac_cfg *cfg);
};


void ac_open(int devid);
void ac_close(int devid);
void ac_read_cfg(int devid, struct ac_cfg *cfg);
void ac_write_cfg(int devid, struct ac_cfg *cfg);













#endif
