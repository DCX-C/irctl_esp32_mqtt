#ifndef _IR_ENCODER_C_
#define _IR_ENCODER_C_

void ir_cwave_off();
void ir_cwave_on();
void ac_swi(int sw);
void ir_io_init(unsigned int pin);
void ir_encoder_init();
struct ac_tcl_basic{
    unsigned int st0;
    unsigned int st1;
    unsigned int cwt;
    unsigned int lg0;
    unsigned int lg1;
    unsigned int data_buf;
    unsigned int data_len;
};














#endif
