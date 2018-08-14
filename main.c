#include <ti/devices/msp432p4xx/driverlib/driverlib.h>
#include <ti/grlib/grlib.h>
#include "LcdDriver/Crystalfontz128x128_ST7735.h"
#include "LcdDriver/HAL_MSP_EXP432P401R_Crystalfontz128x128_ST7735.h"

// Global parameters with current application settings

typedef enum {black, red, green, yellow, blue, magenta, cyan, white} color_t; //enums for color, baud rate, and FSMs
typedef enum {baud9600, baud19200, baud38400, baud57600} UARTBaudRate_t;
typedef enum {stable0, trans0to1, stable1, trans1to0} state_t;
typedef enum {idle, command, commandB, commandF} parseState_t;

#define ASCII2INT -48 //initializing constants
#define INT2ASCII 48
#define STARTROW 2
#define STATUSROW1 0
#define STATUSROW2 1

int rowNum = 0; //list of global variables to keep track of cursor positions and character counter
int colNum = 0;
int charCounter = 0;

UARTBaudRate_t baudRate = 0;//initalization of baud rate and fg/bg colors
color_t fg = 7, bg = 4;
color_t color;
static parseState_t presentState = idle;
uint8_t previousChar = ' ';

//-----------------------------------------------------------------------
// Character Graphics API
//
// The 128*128 pixel screen is partitioned in a grid of 8 rows of 16 characters
// Each character is a plotted in a rectangle of 8 pixels (wide) by 16 pixels (high)
//
// The lower-level graphics functions are taken from the Texas Instruments Graphics Library
//
//            C Application        (this file)
//                   |
//                 GRLIB           (graphics library)
//                   |
//             CrystalFontz Driver (this project, LcdDriver directory)
//                   |
//                font data        (this project, fonts directory)

Graphics_Context g_sContext;

void InitGraphics() { //initalizing graphics, part of code given
    Crystalfontz128x128_Init();
    Crystalfontz128x128_SetOrientation(LCD_ORIENTATION_UP);
    Graphics_initContext(&g_sContext,
                         &g_sCrystalfontz128x128,
                         &g_sCrystalfontz128x128_funcs);
    Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
    Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_BLUE);
    GrContextFontSet(&g_sContext, &g_sFontCmtt16);
    Graphics_clearDisplay(&g_sContext);
}

void LCDClearDisplay() {//clear the LCD display
    Graphics_clearDisplay(&g_sContext);
}

void LCDDrawChar(unsigned row, unsigned col, int8_t c) {//writing to the LCD
    Graphics_drawString(&g_sContext,
                        &c,
                        1,
                        8 * (col % 16),
                        16 * (row % 8),
                        OPAQUE_TEXT);
}

//------------------------------------------
// UART API
//
// We are using EUSCI_A0, which is a user UART in MSP432 that is accessible
// on your laptop as 'XDS 110 Class Application/User UART'. It usually shows up as COM3.
//
// The low-level UART functions are taken from the MSP432 Driverlib, Chapter 24.
//
// The Baud Rate Computation Procedure is taken from the
// User Guide MSP432P4 Microcontroller, Chapter 24, page 915
//
//  Baud rate computation:
//  - System Clock SMCLK in MSP432P4 is 3MHz (Default)
//  - Baud Rate Division Factor N = 3MHz / Baudrate
//      Eg. N9600 = 30000000 / 9600 = 312.5
//  - If N>16 -> oversampling mode
//      Baud Rate Divider              UCBF  = FLOOR(N/16)     = INT(312.5/16)     = 19
//      First modulation stage select  UCBRF = FRAC(N/16) * 16 = FRAC(312.5/16)*16 = 8
//      Second modulation state select UCBRS = (table 24-4 based on FRAC(312.5))   = 0xAA

eUSCI_UART_Config uartConfig = {
     EUSCI_A_UART_CLOCKSOURCE_SMCLK,               // SMCLK Clock Source = 3MHz
     19,                                           // UCBR   = 19
     8,                                            // UCBRF  = 8
     0xAA,                                         // UCBRS  = 0xAA
     EUSCI_A_UART_NO_PARITY,                       // No Parity
     EUSCI_A_UART_LSB_FIRST,                       // LSB First
     EUSCI_A_UART_ONE_STOP_BIT,                    // One stop bit
     EUSCI_A_UART_MODE,                            // UART mode
     EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION // Oversampling
};

void InitUART() {//initializing UART
    UART_initModule(EUSCI_A0_BASE, &uartConfig);
    UART_enableModule(EUSCI_A0_BASE);
    GPIO_setAsPeripheralModuleFunctionInputPin(GPIO_PORT_P1,
        GPIO_PIN2 | GPIO_PIN3, GPIO_PRIMARY_MODULE_FUNCTION);
}

bool UARTHasChar() {//if UART has a char typed in the terminal
    return (UART_getInterruptStatus (EUSCI_A0_BASE, EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG)
                == EUSCI_A_UART_RECEIVE_INTERRUPT_FLAG);
}

uint8_t UARTGetChar() {//get the character from UART
    if (UARTHasChar())
        return UART_receiveData(EUSCI_A0_BASE);
    else
        return 0;
}

bool UARTCanSend() {
    return (UART_getInterruptStatus (EUSCI_A0_BASE, EUSCI_A_UART_TRANSMIT_INTERRUPT_FLAG)
                == EUSCI_A_UART_TRANSMIT_INTERRUPT_FLAG);
}

void UARTPutChar(uint8_t t) {//write the char to the terminal on MOba
    while (!UARTCanSend()) ;
    UART_transmitData(EUSCI_A0_BASE,t);
}

void UARTSetBaud() {//set the proper baud rate of the 4 possible options
    if (baudRate == 0)//baud rate is 9600
    {
        eUSCI_UART_Config uartConfig = {
             EUSCI_A_UART_CLOCKSOURCE_SMCLK,               // SMCLK Clock Source = 3MHz
             19,                                           // UCBR   = 19
             8,                                            // UCBRF  = 8
             0xAA,                                         // UCBRS  = 0xAA
             EUSCI_A_UART_NO_PARITY,                       // No Parity
             EUSCI_A_UART_LSB_FIRST,                       // LSB First
             EUSCI_A_UART_ONE_STOP_BIT,                    // One stop bit
             EUSCI_A_UART_MODE,                            // UART mode
             EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION // Oversampling
        };
        UART_initModule(EUSCI_A0_BASE, &uartConfig);
        UART_enableModule(EUSCI_A0_BASE);
    }
    else if (baudRate == 1)//baud rate is 19200
    {
        eUSCI_UART_Config uartConfig = {
             EUSCI_A_UART_CLOCKSOURCE_SMCLK,               // SMCLK Clock Source = 3MHz
             9,                                           // UCBR   = 19
             12,                                            // UCBRF  = 8
             0x44,                                         // UCBRS  = 0xAA
             EUSCI_A_UART_NO_PARITY,                       // No Parity
             EUSCI_A_UART_LSB_FIRST,                       // LSB First
             EUSCI_A_UART_ONE_STOP_BIT,                    // One stop bit
             EUSCI_A_UART_MODE,                            // UART mode
             EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION // Oversampling
        };
        UART_initModule(EUSCI_A0_BASE, &uartConfig);
        UART_enableModule(EUSCI_A0_BASE);
    }
    else if (baudRate == 2)//baud rate is 38400
    {
        eUSCI_UART_Config uartConfig = {
             EUSCI_A_UART_CLOCKSOURCE_SMCLK,               // SMCLK Clock Source = 3MHz
             4,                                           // UCBR   = 19
             14,                                            // UCBRF  = 8
             0x10,                                         // UCBRS  = 0xAA
             EUSCI_A_UART_NO_PARITY,                       // No Parity
             EUSCI_A_UART_LSB_FIRST,                       // LSB First
             EUSCI_A_UART_ONE_STOP_BIT,                    // One stop bit
             EUSCI_A_UART_MODE,                            // UART mode
             EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION // Oversampling
        };
        UART_initModule(EUSCI_A0_BASE, &uartConfig);
        UART_enableModule(EUSCI_A0_BASE);
    }
    else if (baudRate == 3)//baud rate is 57600
    {
        eUSCI_UART_Config uartConfig = {
             EUSCI_A_UART_CLOCKSOURCE_SMCLK,               // SMCLK Clock Source = 3MHz
             3,                                           // UCBR   = 19
             4,                                            // UCBRF  = 8
             0x04,                                         // UCBRS  = 0xAA
             EUSCI_A_UART_NO_PARITY,                       // No Parity
             EUSCI_A_UART_LSB_FIRST,                       // LSB First
             EUSCI_A_UART_ONE_STOP_BIT,                    // One stop bit
             EUSCI_A_UART_MODE,                            // UART mode
             EUSCI_A_UART_OVERSAMPLING_BAUDRATE_GENERATION // Oversampling
        };
        UART_initModule(EUSCI_A0_BASE, &uartConfig);
        UART_enableModule(EUSCI_A0_BASE);
    }
}

//------------------------------------------
// Red LED API

void InitRedLED() {
    GPIO_setAsOutputPin    (GPIO_PORT_P1,    GPIO_PIN0);   // red LED on Launchpad
    GPIO_setOutputLowOnPin (GPIO_PORT_P1,    GPIO_PIN0);
}

void RedLEDToggle() {
    GPIO_toggleOutputOnPin(GPIO_PORT_P1, GPIO_PIN0);
}

//------------------------------------------
//Color LED

void ColorLEDSet(color_t t) {//set the color based on the color passed in
    switch (t) {
    case black:
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputLowOnPin (GPIO_PORT_P5, GPIO_PIN6);
        break;
    case red:
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputLowOnPin (GPIO_PORT_P5, GPIO_PIN6);
        break;
    case green:
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputLowOnPin (GPIO_PORT_P5, GPIO_PIN6);
        break;
    case yellow:
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputLowOnPin (GPIO_PORT_P5, GPIO_PIN6);
        break;
    case blue:
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputHighOnPin(GPIO_PORT_P5, GPIO_PIN6);
        break;
    case magenta:
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputHighOnPin(GPIO_PORT_P5, GPIO_PIN6);
        break;
    case cyan:
        GPIO_setOutputLowOnPin (GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputHighOnPin(GPIO_PORT_P5, GPIO_PIN6);
        break;
    case white:
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN6);
        GPIO_setOutputHighOnPin(GPIO_PORT_P2, GPIO_PIN4);
        GPIO_setOutputHighOnPin(GPIO_PORT_P5, GPIO_PIN6);
        break;
    }
}

//uses global variable fgs
void LCDSetFgColor() {//function that sets the foreground color based on function call
    if (fg == 0)//c is black
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
    }
    else if (fg == 1)//c is red
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_RED);
    }
    else if(fg == 2)//c is green
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_GREEN);
    }
    else if (fg == 3)//c is yellow
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_YELLOW);
    }
    else if (fg == 4)//c is blue
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_BLUE);
    }
    else if (fg == 5)//c is magenta
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_MAGENTA);
    }
    else if (fg == 6)//c is cyan
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_CYAN);
    }
    else if (fg == 7)//c is white
    {
        Graphics_setForegroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
    }
}

//uses global variable bg
void LCDSetBgColor() {
    if (bg == 0)//c is black
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_BLACK);
    }
    else if (bg == 1)//c is red
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_RED);
    }
    else if(bg == 2)//c is green
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_GREEN);
    }
    else if (bg == 3)//c is yellow
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_YELLOW);
    }
    else if (bg == 4)//c is blue
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_BLUE);
    }
    else if (bg == 5)//c is magenta
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_MAGENTA);
    }
    else if (bg == 6)//c is cyan
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_CYAN);
    }
    else if (bg == 7)//c is white
    {
        Graphics_setBackgroundColor(&g_sContext, GRAPHICS_COLOR_WHITE);
    }
}

void InitTimerDebounce() {//debounce timer
    Timer32_initModule(TIMER32_0_BASE, TIMER32_PRESCALER_1, TIMER32_32BIT, TIMER32_PERIODIC_MODE);
}

void TimerDebounceStartOneShot() {//starting the timer
    Timer32_setCount(TIMER32_0_BASE, 300000);  // 100ms second period on 3MHz clock
    Timer32_startTimer(TIMER32_0_BASE, true);
}

int TimerDebounceExpiredOneShot() {//if timer is expired or not
    return (Timer32_getValue(TIMER32_0_BASE) == 0);
}

void Init200msTimer() {//init timer for extra credit portion 200 ms timer
    Timer32_initModule(TIMER32_1_BASE, TIMER32_PRESCALER_1, TIMER32_32BIT, TIMER32_PERIODIC_MODE);
}

void Timer200msStartOneShot() {//start timer for 200 ms
    Timer32_setCount(TIMER32_1_BASE, 600000);  // 200ms second period on 3MHz clock
    Timer32_startTimer(TIMER32_1_BASE, true);
}

int Timer200msExpiredOneShot() {//if timer has expired or not
    return (Timer32_getValue(TIMER32_1_BASE) == 0);
}

//------------------------------------------
// Debounce FSM for S2
//

bool BounceFSM(bool *button) {

    /*stable0, trans0to1, stable1, trans1to0*/
    static state_t currentState = stable0;

    bool checkTimerExpiration = TimerDebounceExpiredOneShot();//bool to check if timer expired or not

    bool debounced = false;
    bool timerStart = false;

    switch (currentState)
    {
    case stable0:
        if (*button == 1)
        {
            currentState = trans0to1;
            timerStart = true;
        }
        break;

    case trans0to1:
        if (*button == 0)
        {
            currentState = stable0;
        }
        else
        {
            if (checkTimerExpiration == 1)
            {
                currentState = stable1;
            }
        }
        break;

    case stable1:

        debounced = true;

        if (*button == 0)
        {
            currentState = trans1to0;
            timerStart = true;
        }
        break;

    case trans1to0:

        debounced = true;

        if (*button == 1)
        {
            currentState = stable1;
        }
        else
        {
            if (checkTimerExpiration)
            {
                currentState = stable0;
            }
        }
        break;
    }
        if (timerStart)
        {
            TimerDebounceStartOneShot();
        }
        return debounced;
}

//------------------------------------------------
//BUTTON API
void InitButtonS1() {//init button 1 for clear/display
    GPIO_setAsInputPin (GPIO_PORT_P5, GPIO_PIN1); // upper switch S1 on BoostXL
}

bool ButtonS1Pressed() {//if button is pressed or not
    return (GPIO_getInputPinValue(GPIO_PORT_P5, GPIO_PIN1) == 0);
}

void InitButtonS2() {//init button 2 for setting baud rate
    GPIO_setAsInputPin (GPIO_PORT_P3, GPIO_PIN5); // lower switch S2 on BoostXL
}

int ButtonS2Pressed() {//if button 2 is pressed or not
    return (GPIO_getInputPinValue(GPIO_PORT_P3, GPIO_PIN5) == 0);
}

//Functions that I have written for implementation
void write2LCD(uint8_t inChar)//function
{
    //the following code writes to the LCD display
       LCDDrawChar(rowNum, colNum, inChar);
       colNum += 1;

       if (colNum == 16)//used to print to next row and wrapping around
       {
           colNum = 0;
           rowNum += 1;
       }
}//end of outputting to LCD display

void printMessageLCD()
{
    int col = 0;

    if (baudRate == 0)//determing baud rate and print accordingly to LCD
    {
        LCDDrawChar(STATUSROW1, col, 'b');
        LCDDrawChar(STATUSROW1, col+1, 'd');
        LCDDrawChar(STATUSROW1, col+2, ' ');
        LCDDrawChar(STATUSROW1, col+3, '9');
        LCDDrawChar(STATUSROW1, col+4, '6');
        LCDDrawChar(STATUSROW1, col+5, '0');
        LCDDrawChar(STATUSROW1, col+6, '0');
    }
    else if (baudRate == 1)
    {
        LCDDrawChar(STATUSROW1, col, 'b');
        LCDDrawChar(STATUSROW1, col+1, 'd');
        LCDDrawChar(STATUSROW1, col+2, '1');
        LCDDrawChar(STATUSROW1, col+3, '9');
        LCDDrawChar(STATUSROW1, col+4, '2');
        LCDDrawChar(STATUSROW1, col+5, '0');
        LCDDrawChar(STATUSROW1, col+6, '0');
    }
    else if (baudRate == 2)
    {
        LCDDrawChar(STATUSROW1, col, 'b');
        LCDDrawChar(STATUSROW1, col+1, 'd');
        LCDDrawChar(STATUSROW1, col+2, '3');
        LCDDrawChar(STATUSROW1, col+3, '8');
        LCDDrawChar(STATUSROW1, col+4, '4');
        LCDDrawChar(STATUSROW1, col+5, '0');
        LCDDrawChar(STATUSROW1, col+6, '0');
    }
    else if (baudRate == 3)
    {
        LCDDrawChar(STATUSROW1, col, 'b');
        LCDDrawChar(STATUSROW1, col+1, 'd');
        LCDDrawChar(STATUSROW1, col+2, '5');
        LCDDrawChar(STATUSROW1, col+3, '7');
        LCDDrawChar(STATUSROW1, col+4, '6');
        LCDDrawChar(STATUSROW1, col+5, '0');
        LCDDrawChar(STATUSROW1, col+6, '0');
    }

    LCDDrawChar(STATUSROW1, col+9, 'f');//print fg color number and print number as char
    LCDDrawChar(STATUSROW1, col+10, 'g');
    LCDDrawChar(STATUSROW1, col+11, fg + '0');

    LCDDrawChar(STATUSROW1, col+13, 'b');//print bg color number and print number as char
    LCDDrawChar(STATUSROW1, col+14, 'g');
    LCDDrawChar(STATUSROW1, col+15, bg + '0');

    LCDDrawChar(STATUSROW2, col, 'n');

    int one, two, three, four;//variables for each digit

    one = charCounter / 1000;//calculations to extract each digit
    two = charCounter / 100 % 10;
    three = charCounter /10 % 10;
    four = charCounter % 10;

    LCDDrawChar(STATUSROW2, col+1, ' ');//print each digit as char
    LCDDrawChar(STATUSROW2, col+2, one + '0');
    LCDDrawChar(STATUSROW2, col+3, two + '0');
    LCDDrawChar(STATUSROW2, col+4, three + '0');
    LCDDrawChar(STATUSROW2, col+5, four + '0');

    rowNum = STATUSROW2 + 1;//start on row 2 col 0 after displaying status message
    colNum = 0;
}

void printMessageUART()
{
    int col = 0;

    if (baudRate == 0)//determing baud rate and print accordingly to LCD
    {
        UARTPutChar('b');
        UARTPutChar('d');
        UARTPutChar(' ');
        UARTPutChar('9');
        UARTPutChar('6');
        UARTPutChar('0');
        UARTPutChar('0');
    }
    else if (baudRate == 1)
    {
        UARTPutChar('b');
       UARTPutChar('d');
       UARTPutChar('1');
       UARTPutChar('9');
       UARTPutChar('2');
       UARTPutChar('0');
       UARTPutChar('0');
    }
    else if (baudRate == 2)
    {
        UARTPutChar('b');
       UARTPutChar('d');
       UARTPutChar('3');
       UARTPutChar('8');
       UARTPutChar('4');
       UARTPutChar('0');
       UARTPutChar('0');
    }
    else if (baudRate == 3)
    {
        UARTPutChar('b');
       UARTPutChar('d');
       UARTPutChar('5');
       UARTPutChar('7');
       UARTPutChar('6');
       UARTPutChar('0');
       UARTPutChar('0');
    }

    UARTPutChar(' ');//print fg color number and print number as char
    UARTPutChar('f');
    UARTPutChar('g');
    UARTPutChar(fg + '0');

    UARTPutChar(' ');//print bg color number and print number as char
    UARTPutChar('b');
    UARTPutChar('g');
    UARTPutChar(bg + '0');

    UARTPutChar(' ');
    UARTPutChar('n');

    int one, two, three, four;//variables for each digit

    one = charCounter / 1000;//calculations to extract each digit
    two = charCounter / 100 % 10;
    three = charCounter /10 % 10;
    four = charCounter % 10;

    UARTPutChar(one + '0');//print each digit as char
    UARTPutChar(two + '0');
    UARTPutChar(three + '0');
    UARTPutChar(four + '0');
}

void checkButton2Status(bool *button, bool *prev_button, bool *prev_buttonDebounce, bool *buttonDebounce)
{
    *prev_button = *button;
    *prev_buttonDebounce = *buttonDebounce;

    *button = ButtonS2Pressed();

    *buttonDebounce = BounceFSM(button);

    if (*buttonDebounce & !(*prev_buttonDebounce))//if button 2 pressed
    {
        baudRate += 1; //increment baud rate by 1
        if (baudRate == 4)//if max baud rate
        {
            baudRate = baud9600;//go back to 9600
        }
        UARTSetBaud();//set baud rate
    }
}

void parseCommand(uint8_t c)
{
    switch(presentState)
    {
    case idle:
        if (c == '#')//if c is #, potential command
        {
            presentState = command;
            previousChar = c;
        }
        else//write to LCD and UART accordingly
        {
            write2LCD(c);
            UARTPutChar(c);
        }
        break;

    case command:
        if (c == 'f')//potential fg command
        {
            presentState = commandF;
        }
        else if (c == 'b')//potential bg command
        {
            presentState = commandB;
        }
        else //not a valid command, print what characters says
        {
            if (c == ' ')
           {
               presentState = idle;
               break;
           }
           write2LCD('#');
           UARTPutChar('#');
           write2LCD(c);
           UARTPutChar(c);
           presentState = idle;
        }
        break;

    case commandF:
        if (c >= '0' && c <= '7')//if valid enum number
        {
            fg = c - '0';
            LCDSetFgColor();//set fg color
            presentState = idle;//go back to idle
        }
        else//not a real command, print out every character
        {
            write2LCD('#');
            UARTPutChar('#');
            previousChar = 'f';
            write2LCD(previousChar);
            UARTPutChar(previousChar);
            write2LCD(c);
            UARTPutChar(c);
            presentState = idle;
        }
        break;

    case commandB:
        if (c >= '0' && c <= '7')//if valid enum number
        {
            bg = c - '0';
            LCDSetBgColor();//set bg color
            presentState = idle;
        }
        else//write normally with every character
        {
            write2LCD('#');
            UARTPutChar('#');
            previousChar = 'b';
            write2LCD(previousChar);
            UARTPutChar(previousChar);
            write2LCD(c);
            UARTPutChar(c);
            presentState = idle;
        }
        break;
    }
}

void InitColorLED()//initalize booster board LED
{
    GPIO_setAsOutputPin    (GPIO_PORT_P2,    GPIO_PIN6);   // color LED red on sensor board
    GPIO_setAsOutputPin    (GPIO_PORT_P2,    GPIO_PIN4);   // color LED green on sensor board
    GPIO_setAsOutputPin    (GPIO_PORT_P5,    GPIO_PIN6);   // color LED blue on sensor board

    GPIO_setOutputLowOnPin (GPIO_PORT_P2,    GPIO_PIN6);
    GPIO_setOutputLowOnPin (GPIO_PORT_P2,    GPIO_PIN4);
    GPIO_setOutputLowOnPin (GPIO_PORT_P5,    GPIO_PIN6);
}

void LEDchange(uint8_t character)
{
    Init200msTimer();//init timers and color LEDS
    InitColorLED();

    if (character == '#')//hash character, white LED
    {
        color = white;
        Timer200msStartOneShot();//start timer
        ColorLEDSet(color);
    }
    else if (character >= '0' && character <= '9')//number, red LED
    {
        color = red;
        Timer200msStartOneShot();//start timer
        ColorLEDSet(color);
    }
    else if (character >= 'A' && character <= 'Z' || character >= 'a' && character <= 'z')//letter, blue LED
    {
        color = blue;
        Timer200msStartOneShot();
        ColorLEDSet(color);//change LED
    }
    else //none of the above, green LED
    {
        color = green;
        Timer200msStartOneShot();
        ColorLEDSet(color);
    }
}

//-----------------------------------------------------------------------

int main(void) {
    uint8_t c;

    WDT_A_hold(WDT_A_BASE);

    InitGraphics();//all inits
    InitUART();
    InitRedLED();
    InitButtonS1();
    InitButtonS2();
    InitColorLED();
    Init200msTimer();
    InitTimerDebounce();

    bool buttonDebounce = false, prev_buttonDebounce;
    bool button = false, prev_button;//init variables

    while (1)
    {
        if (Timer200msExpiredOneShot())//if timer expired
        {
            ColorLEDSet(black);//turn LED off
        }

        if(UARTHasChar())//if char available on UART
        {
            c = UARTGetChar();//get char
            charCounter++;//increment
            LEDchange(c);//change LED on booster
            parseCommand(c);//send char to parse
        }

        checkButton2Status(&button, &prev_button, &prev_buttonDebounce, &buttonDebounce);//if button is pressed change baud rate

        if (ButtonS1Pressed())//if button 1 pressed
        {
            LCDClearDisplay();//clear display
            printMessageLCD();//print status message on LCD
            printMessageUART();//also print on UART
        }
    }
}
