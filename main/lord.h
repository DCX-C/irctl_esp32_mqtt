#ifndef _LORD_H_
#define _LORD_H_


enum lorn_status {
    LORD_LEFT,
    LORD_COME,
    LORD_COLD, 
    LORD_HOT,
};

struct lord {
    int status;
};

#endif