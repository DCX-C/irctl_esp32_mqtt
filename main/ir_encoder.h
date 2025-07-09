#ifndef _IR_ENCODER_C_
#define _IR_ENCODER_C_

void ir_cwave_off();
void ir_cwave_on();
void ac_swi(int sw);
void ir_io_init(unsigned int pin);
void ir_encoder_init();
int ac_is_open();
int ac_is_fixed();
int ac_fixed_tgl();
void ac_tup();
void ac_tdown();
void ac_set_temperature(int t);
int ac_get_temperature();

struct ac_tcl_basic{
    unsigned int st0;
    unsigned int st1;
    unsigned int cwt;
    unsigned int lg0;
    unsigned int lg1;
    unsigned char *data_buf;
    unsigned int data_len;
    unsigned int is_open : 1,
                 is_fixed: 1,
                 temp    : 6,
                 pin     : 8,
                 rev     : 16;
};














#endif
