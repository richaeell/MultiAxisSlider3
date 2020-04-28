#include <Arduino.h>

// #define debug_com
#define debug_control_msg

#define T1_FREQ 656250
#define MIN_DELAY 200
#define TIMEOUT 500

#define PIN_2_DIR 55    // PORTA Bit 24
#define PIN_1_ENABLE 56 // PORTA Bit 23
#define PIN_2_STEP 54   // PORTA Bit 16
#define PIN_0_MIN 18    // PORTA Bit 11
#define PIN_0_MAX 19    // PORTA Bit 10
#define PIN_1_STEP 60   // PORTA Bit 3
#define PIN_1_DIR 61    // PORTA Bit 2

#define PIN_2_MAX 2     // PORTB Bit 25
#define PIN_0_ENABLE 62 // PORTB Bit 17

#define PIN_2_MIN 3     // PORTC Bit 28
#define PIN_FAN 9       // PORTC Bit 21
#define PIN_0_STEP 46   // PORTC Bit 17
#define PIN_0_DIR 48    // PORTC Bit 15
#define PIN_2_ENABLE 38 // PORTC Bit 6

#define PIN_1_MAX 15 // PORTD Bit 5
#define PIN_1_MIN 14 // PORTD Bit 4

// Direction of stepper axis1 movement
#define CW -1
#define CCW 1

float t = 0.0;
float a = 0.0;
float b = 0.0;
float c = 0.0; 
long packetTimer = 0;

union payload {

  int32_t numbers[4];
  uint8_t array[12];

} m_payload;

union integer {

  int32_t number;
  uint8_t array[4];

} m_integer;

enum protocolState
{
  LISTENING, // blink disable
  READ_LOAD, // blink enable
  READ_END,  // we want the led to be on for interval
  END_CMD    // we want the led to be off for interval
};

typedef struct
{
  int max_vel;
  // Hardware declarations
  uint8_t enable_pin;
  uint8_t step_pin;
  uint8_t dir_pin;

  // Motor status at any given time
  volatile int8_t dir; //! Direction stepper axis1 should move.
  volatile int32_t step_position;

  // Interrupt variables
  volatile uint32_t step_delay; //! Peroid of next timer delay. At start this value set the accelration rate c0.
  volatile uint32_t min_delay;  //! Minimum time delay (max speed)

  // Interrupt handler
  Tc *tc;
  uint32_t channel;
  IRQn_Type irq;

} m_motor_data;

m_motor_data axes[3];

void startTimer(Tc *tc, uint32_t channel, IRQn_Type irq)
{
  NVIC_ClearPendingIRQ(irq);
  NVIC_EnableIRQ(irq);
  TC_Start(tc, channel);
}

void stopTimer(Tc *tc, uint32_t channel, IRQn_Type irq)
{
  NVIC_DisableIRQ(irq);
  TC_Stop(tc, channel);
}

void restartCounter(Tc *tc, uint32_t channel)
{
  // To reset a conter we se the TC_CCR_SWTRG (Software trigger) bit in the TC_CCR
  tc->TC_CHANNEL[channel].TC_CCR |= TC_CCR_SWTRG;
}

void configureTimer(Tc *tc, uint32_t channel, IRQn_Type irq, uint32_t frequency)
{
  // Unblock thee power managent cotroller
  pmc_set_writeprotect(false);
  pmc_enable_periph_clk((uint32_t)irq);
  // Configure
  TC_Configure(tc,           // Timer
               channel,      // Channel
               TC_CMR_WAVE | // Wave form is enabled
                   TC_CMR_WAVSEL_UP_RC |
                   TC_CMR_TCCLKS_TIMER_CLOCK4 // Settings
  );

  uint32_t rc = VARIANT_MCK / 128 / frequency; //128 because we selected TIMER_CLOCK4 above

  tc->TC_CHANNEL[channel].TC_RC = rc; //TC_SetRC(tc, channel, rc);
  // TC_Start(tc, channel);

  // enable timer interrupts on the timer
  tc->TC_CHANNEL[channel].TC_IER = TC_IER_CPCS;  // IER = interrupt enable register // Enables the RC compare register.
  tc->TC_CHANNEL[channel].TC_IDR = ~TC_IER_CPCS; // IDR = interrupt disable register /// Disables the RC compare register.

  // To reset a conter we se the TC_CCR_SWTRG (Software trigger) bit in the TC_CCR
  tc->TC_CHANNEL[channel].TC_CCR |= TC_CCR_SWTRG;

  /* Enable the interrupt in the nested vector interrupt controller */
  // NVIC_EnableIRQ(irq);
}

void TC3_Handler()
{
  TC_GetStatus(axes[0].tc, axes[0].channel); // Timer 1 channel 0 ----> TC3 it also clear the flag
  digitalWrite(axes[0].step_pin, HIGH);
  digitalWrite(axes[0].step_pin, LOW);
  axes[0].step_position += axes[0].dir;
  // axes[0].tc->TC_CHANNEL[axes[0].channel].TC_RC = axes[0].step_delay;
}

void TC4_Handler()
{
  TC_GetStatus(axes[1].tc, axes[1].channel); // Timer 1 channel 0 ----> TC3 it also clear the flag
  digitalWrite(axes[1].step_pin, HIGH);
  digitalWrite(axes[1].step_pin, LOW);
  axes[1].step_position += axes[1].dir;
  // axes[1].tc->TC_CHANNEL[axes[1].channel].TC_RC = axes[1].step_delay;
}

void TC5_Handler()
{
  TC_GetStatus(axes[2].tc, axes[2].channel); // Timer 1 channel 0 ----> TC3 it also clear the flag
  digitalWrite(axes[2].step_pin, HIGH);      // Step
  digitalWrite(axes[2].step_pin, LOW);
  axes[2].step_position += axes[2].dir; // Get motor position data
  // axes[2].tc->TC_CHANNEL[axes[2].channel].TC_RC = axes[2].step_delay;
}

void setup()
{
  Serial.begin(9600);
  SerialUSB.begin(9600);
  delay(3000);

  pinMode(PIN_0_ENABLE, OUTPUT);
  pinMode(PIN_0_DIR, OUTPUT);
  pinMode(PIN_0_STEP, OUTPUT);

  pinMode(PIN_1_ENABLE, OUTPUT);
  pinMode(PIN_1_DIR, OUTPUT);
  pinMode(PIN_1_STEP, OUTPUT);

  pinMode(PIN_2_ENABLE, OUTPUT);
  pinMode(PIN_2_DIR, OUTPUT);
  pinMode(PIN_2_STEP, OUTPUT);

  delay(3000);

  SerialUSB.println("Starting timer..");

  configureTimer(/*Timer TC1*/ TC1, /*Channel 0*/ 0, /*TC3 interrupt nested vector controller*/ TC3_IRQn, /*Frequency in hz*/ T1_FREQ);
  configureTimer(/*Timer TC1*/ TC1, /*Channel 0*/ 1, /*TC3 interrupt nested vector controller*/ TC4_IRQn, /*Frequency in hz*/ T1_FREQ);
  configureTimer(/*Timer TC1*/ TC1, /*Channel 0*/ 2, /*TC3 interrupt nested vector controller*/ TC5_IRQn, /*Frequency in hz*/ T1_FREQ);

  // Axis 1
  axes[0].enable_pin = PIN_0_ENABLE;
  axes[0].step_pin = PIN_0_STEP;
  axes[0].dir_pin = PIN_0_DIR;
  axes[0].max_vel = 15000; // 30k is the max
  axes[0].tc = TC1;
  axes[0].channel = 0;
  axes[0].irq = TC3_IRQn;
  axes[0].min_delay = T1_FREQ / axes[0].max_vel;

  // Axis 2
  axes[1].enable_pin = PIN_1_ENABLE;
  axes[1].step_pin = PIN_1_STEP;
  axes[1].dir_pin = PIN_1_DIR;
  axes[1].max_vel = 15000; // MAX for this axes
  axes[1].tc = TC1;
  axes[1].channel = 1;
  axes[1].irq = TC4_IRQn;
  axes[1].min_delay = T1_FREQ / axes[1].max_vel;

  // Axis 3
  axes[2].enable_pin = PIN_2_ENABLE;
  axes[2].step_pin = PIN_2_STEP;
  axes[2].dir_pin = PIN_2_DIR;
  axes[2].max_vel = 15000; // MAX for this axes
  axes[2].tc = TC1;
  axes[2].channel = 2;
  axes[2].irq = TC5_IRQn;
  axes[2].min_delay = T1_FREQ / axes[2].max_vel;

  digitalWrite(PIN_0_ENABLE, LOW);
  digitalWrite(PIN_1_ENABLE, LOW);
  digitalWrite(PIN_2_ENABLE, LOW);

  axes[2].tc->TC_CHANNEL[axes[2].channel].TC_RC = 100000;
  axes[1].tc->TC_CHANNEL[axes[1].channel].TC_RC = 100000;
  axes[0].tc->TC_CHANNEL[axes[0].channel].TC_RC = 100000;

  startTimer(axes[2].tc,axes[2].channel,axes[2].irq);
  startTimer(axes[1].tc,axes[1].channel,axes[1].irq);
  startTimer(axes[0].tc,axes[0].channel,axes[0].irq);

  SerialUSB.print("\nReady");
}

void loop()
{
  if(millis()-packetTimer>TIMEOUT){
    stopTimer(axes[2].tc,axes[2].channel,axes[2].irq);
    stopTimer(axes[1].tc,axes[1].channel,axes[1].irq);
    stopTimer(axes[0].tc,axes[0].channel,axes[0].irq);
    digitalWrite(PIN_0_ENABLE, HIGH);
    digitalWrite(PIN_1_ENABLE, HIGH);
    digitalWrite(PIN_2_ENABLE, HIGH);
  }

  if (Serial.available())
  {
#ifdef debug_com
    SerialUSB.println("\nIncomming data:");
#endif

    int16_t byten = 11;
    uint8_t state = LISTENING;
    uint8_t new_packet = 0;
    do
    {
      while (Serial.available() == 0)
        ;                              // Wait for incomming data // ADD TIMEOUT GOD DAMN IT
      uint8_t in_byte = Serial.read(); // READ BYTE
#ifdef debug_com
      SerialUSB.print("State: " + String(state) + " Byte: ");
      SerialUSB.print(in_byte, HEX);
      SerialUSB.println();
#endif
      switch (state)
      {
      case LISTENING:
        if (in_byte == 0x7E) // CHECK IF ITS START BYTE
        {
#ifdef debug_com
          SerialUSB.println("\n START command read");
#endif

          state = READ_LOAD;
        }
        else
        {
// Wrong start command
#ifdef debug_com
          SerialUSB.println("\nWrong START BYTE");
#endif

          state = END_CMD; // RESET IF IT ISN'T
        }
        break;

      case READ_LOAD:
        // Serial.readBytes(m_payload.array,12);
        m_payload.array[byten] = in_byte;
        byten--;
        if (byten < 0)
        {
#ifdef debug_com
          SerialUSB.println("\nRecived 12 bytes");
#endif

          // for(int i=0;i<12;i++)
          //   SerialUSB.print(m_payload.array[i],HEX);
          state = READ_END;
          byten = 3;
        }
        break;

      case READ_END:
        // Serial.readBytes(m_integer.array,4);
        m_integer.array[byten] = in_byte;
        byten--;
        if (byten < 0)
        {
#ifdef debug_com
          SerialUSB.println("\nRecived end command");
          for (int i = 0; i < 4; i++)
            SerialUSB.print(m_integer.array[i], HEX);
          SerialUSB.println();
          SerialUSB.println(m_integer.number, HEX);
#endif

          if (m_integer.number == (int32_t)0x7FFFFFFF) // END COMMAND
          {
            // Data is correct
            new_packet = 1;
            state = END_CMD;
#ifdef debug_com
            SerialUSB.println("\nEnd packet");
#endif
          }
          else
          {
#ifdef debug_com
            SerialUSB.println("\nWrong packet");
#endif

            state = END_CMD;
          }
        }
        break;
      }
    } while (state != END_CMD);

    if (new_packet)
    {
      new_packet = 0;
      packetTimer = millis();

      digitalWrite(PIN_0_ENABLE, LOW);
      digitalWrite(PIN_1_ENABLE, LOW);
      digitalWrite(PIN_2_ENABLE, LOW);

      // GET AND SET DIRECTION VALUES
      digitalWrite(PIN_0_DIR,m_payload.numbers[2]>0? HIGH:LOW);
      digitalWrite(PIN_1_DIR,m_payload.numbers[1]>0? HIGH:LOW);
      digitalWrite(PIN_2_DIR,m_payload.numbers[0]>0? HIGH:LOW);

      axes[0].dir = m_payload.numbers[2] > 0 ? CCW : CW;
      axes[1].dir = m_payload.numbers[1] > 0 ? CCW : CW; 
      axes[2].dir = m_payload.numbers[0] > 0 ? CCW : CW; 

      // ABS VALUES
      m_payload.numbers[2] = abs(m_payload.numbers[2]);
      m_payload.numbers[1] = abs(m_payload.numbers[1]);
      m_payload.numbers[0] = abs(m_payload.numbers[0]);

#ifdef debug_control_msg
      SerialUSB.println("Saturate and start/stop timers");
#endif

      // CALCULATE DELAYS AND SET REGISTER
      if(m_payload.numbers[2] > MIN_DELAY){
        stopTimer(axes[0].tc,axes[0].channel,axes[0].irq);
        axes[0].tc->TC_CHANNEL[axes[0].channel].TC_RC = (uint32_t)m_payload.numbers[2];
        startTimer(axes[0].tc,axes[0].channel,axes[0].irq);
      }
      if(m_payload.numbers[1] > MIN_DELAY){
        stopTimer(axes[1].tc,axes[1].channel,axes[1].irq);
        axes[1].tc->TC_CHANNEL[axes[1].channel].TC_RC = (uint32_t)m_payload.numbers[1];
        startTimer(axes[1].tc,axes[1].channel,axes[1].irq);
      }
      if(m_payload.numbers[0] > MIN_DELAY){
        stopTimer(axes[2].tc,axes[2].channel,axes[2].irq);
        axes[2].tc->TC_CHANNEL[axes[2].channel].TC_RC = (uint32_t)m_payload.numbers[0];
        startTimer(axes[2].tc,axes[2].channel,axes[2].irq);
      }
#ifdef debug_control_msg
        SerialUSB.println();
        SerialUSB.print("Delay M1 = ");
        SerialUSB.print(m_payload.numbers[2]);
        SerialUSB.print(" M2 = ");
        SerialUSB.print(m_payload.numbers[1]);
        SerialUSB.print(" M3 = ");
        SerialUSB.println(m_payload.numbers[0]);
#endif

    }
  }
}
