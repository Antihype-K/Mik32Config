#ifndef _BOARD_H_
#define _BOARD_H_

extern volatile bool use_error_messages;
#define DISABLE_ERROR_MESSAGES()    (use_error_messages = false)
#define ENABLE_ERROR_MESSAGES()     (use_error_messages = true)

// function for init called after setup()
void post_init(void);

#endif /* _BOARD_H_ */
