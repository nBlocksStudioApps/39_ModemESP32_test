#include "uart_at_master.h"

DigitalOut led1(LED1);
DigitalOut led2(LED2);
DigitalOut led3(LED3);
DigitalOut led4(LED4);

Serial debugpc(USBTX, USBRX);

ATMaster::ATMaster(PinName txPin, PinName rxPin, char * ok_prefix, char * error_prefix): 
    _uart(txPin, rxPin) {
    _at_ok_prefix = ok_prefix;
    _at_error_prefix = error_prefix;
    _line_mode = AT_LINE_CRLF;
    
    _buf_message_len = 0;
    _has_data = 0;
    _wait_init = 0;

    _uart.baud(115200);

    _uart.attach(callback(this, &ATMaster::_rx_irq), Serial::RxIrq);
    
    expected_length = 0;
}

void ATMaster::set_init_prefix(char * init_prefix) {
    _at_init_prefix = init_prefix;
    _wait_init = 1;
}

void ATMaster::set_custom_prefix(char * custom_prefix) {
    _at_custom_prefix = custom_prefix;
}

void ATMaster::set_line_mode(_AT_LINE_MODE new_mode) {
    _line_mode = new_mode;
    // If we are changing to content length mode, but the buffer has
    // *already* received the data we will be expecting, we mark it
    // right away. For this to work, expected_length must be set
    // BEFORE changing the mode
    if (new_mode == AT_LINE_LENGTH) {
        if (_buf_message_len >= expected_length) {
            _buf_add(0);
            _has_data++;
            _buf_message_len = 0;
        }
    }
}

void ATMaster::_rx_irq() {
    if (_uart.readable()) {
        // Retrieve current char
        char recv = _uart.getc();
        led1 = !led1;
        if (recv == 0) led4 = !led4;
        
        switch(_line_mode) {

            case AT_LINE_CRLF:
                // "\r\n" system:
                // When '\r' is received, we do not immediately add it to buffer
                // instead we wait the next char. If next is '\n', we have a new
                // line, we discard both and mark buffer as containing a line.
                // If next is anything else, the '\r' is a normal character and we
                // add both.
                
                // When '\r' itself is received:
                if (recv == '\r') {
                    // if the previous character was also a \r, it was normal data
                    // and we add it to buffer since it was not yet added
                    if (_last_received_char == '\r') _buf_add(_last_received_char);
                }
                // If we received a '\n' after a '\r':
                else if ((recv == '\n') && (_last_received_char == '\r')) {
                    // Both will be discarded and current buffer is marked as a line
                    // a NULL character is added instead to mark end of string

                    // If we have data, add a null termination
                    //if (_buf_message_len) {
                        _buf_add(0);
                    //}
                    // but if we don't have any actual data, just keep buffer empty

                    _has_data++;
                    _buf_message_len = 0;
                    led2 = !led2;
                }
                else {
                    _buf_add(recv);
                    _buf_message_len++;
                }
                
                _last_received_char = recv;
                
                break;
        

            
            case AT_LINE_LF:
                // "\n" system:
                // All chars but '\n' are data chars
                if (recv == '\n') {
                    // Current '\n' char is discarded and current buffer is marked as a line
                    // a NULL character is added instead to mark end of string

                    // Add a null termination
                    _buf_add(0);

                    _has_data++;
                    _buf_message_len = 0;
                    led2 = !led2;
                }
                else {
                    _buf_add(recv);
                    _buf_message_len++;
                }
                
                break;
                
            case AT_LINE_LENGTH:
                // Content length system:
                // Everything is added to buffer until the expected
                // length is achieved or operation is cancelled
                
                _buf_add(recv);
                _buf_message_len++;
                debugpc.putc(recv);
                debugpc.printf(" (%d)\n", _buf_message_len);
                if (_buf_message_len >= expected_length) {
                    _buf_add(0);
                    _has_data++;
                    _buf_message_len = 0;
                    
                }
                
                
                break;
        }
        
    }
}
void ATMaster::_tx_irq() {
}


uint32_t ATMaster::has_data() {
    return _has_data;
}

_AT_RESPONSE_TYPE ATMaster::process(char * destination_string) {
    if (!_has_data) return AT_RESPONSE_NONE;
    
    _AT_RESPONSE_TYPE result;
    _has_data--;
   
    
    if (_wait_init && buf_compare(_at_init_prefix, false)) {
        result = AT_RESPONSE_INIT;
        _wait_init = 0;
    }
    
    else if (buf_compare(_at_ok_prefix, false)) {
        result = AT_RESPONSE_OK;
    }

    else if (buf_compare(_at_error_prefix, false)) {
        result = AT_RESPONSE_ERROR;
    }

    else if (buf_compare(_at_custom_prefix, false)) {
        result = AT_RESPONSE_CUSTOM;
    }
    
    else result = AT_RESPONSE_OTHER;
    
    uint32_t i = 0;
    if (_buf_len) {
        // Dispose current message from the buffer
        // When this method is called it is necessarily null-terminated
        // The _buf_take() call in the end of the 'while' block will also
        // make sure the null is removed leaving the buf clean
        char c = _buf_take();
        while (c) {
            // If a destination string buffer was provided, fill it with
            // current payload line
            // Buffer adequate sizing is user responsibility
            if (destination_string) destination_string[i++] = c;
            
            // Next char or null
            c = _buf_take();
        }
        // If we are filling an external buffer, we have to null-terminate it
        if (destination_string) destination_string[i] = 0;
    }
    if (i == 0) {
        // There was no content, this is a blank line
        result = AT_RESPONSE_BLANKLINE;
    }
    
    return result;
}




void ATMaster::_buf_add(char value) {
    if (_buf_len < __AT_MASTER_BUF_SIZE) {
        _buf[_buf_head] = value;
        _buf_len++;
        _buf_head++;
        if (_buf_head >= __AT_MASTER_BUF_SIZE) _buf_head = 0;
    }
}

char ATMaster::_buf_take() {
    char value;
    if (_buf_len > 0) {
        value = _buf[_buf_tail];
        _buf_len--;
        _buf_tail++;
        if (_buf_tail >= __AT_MASTER_BUF_SIZE) _buf_tail = 0;
        return value;
    }
    else return 0; 
}

uint32_t ATMaster::buf_compare(const char * value, bool exact_length) {
    uint32_t i_buf = _buf_tail;
    uint32_t i=0;
        
    //_uart.printf("<");

    // For every non-null character of value
    while (value[i]) {
       // _uart.putc(_buf[i_buf]);
        
        // If buf payload is shorter result is false
        if (i >= _buf_len) return 0;
        
        // Any mismatch returns false
        if (value[i] != _buf[i_buf]) return 0;
        
        // Move cursors
        i++;
        i_buf++;
        if (i_buf >= __AT_MASTER_BUF_SIZE) i_buf = 0;
    }
    // If we need exact length and buf is longer, return false
    if (exact_length && (_buf_len > i)) return 0;
    
    //_uart.printf(">");
    // If nothing failed, returns true
    return 1;
}