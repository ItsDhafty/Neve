#ifndef NV_ERROR_H_
#define NV_ERROR_H_

// Type of errors
typedef enum ErrType {
    ErrType_None = 0, // No error occurred
    ErrType_Errno, // An internal error set by a C function
    ErrType_CustomMsg // An error with a custom message in `data.customMsg`
} ErrType;

// The error of a function call
typedef struct Err {
    ErrType type; // The type of the error
    union {
        const char *customMsg;
    } data;
} Err;

// Get the current error.
Err *errGet(void);
// Print the current error to stderr.
void errLog(const char *msg);

// Set the error as a system error.
void errSetErrno(void);
// Set a custom error message (it should live until the end of the program).
void errSetMsg(const char *msg);
// Clear the error.
void errClear(void);

#endif // !NV_ERROR_H_
