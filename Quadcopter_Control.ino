#define debug 0
#include "I2Cdev.h"
#include "MPU6050_6Axis_MotionApps20.h"
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
#include "Wire.h"
#endif

#include "Controller.h"
#include "printf.h"


//THIS IS YOUR TEAM NUMBER
#define channel 21
#define PALevel RF24_PA_HIGH
#define CE A0
#define CS A1
#define led 3
#define led2 4

#define minPWM 0
#define maxPWM 70
#define MINPITCH 20
#define MAXPITCH 245
#define MINYAW 20

const float pi = 3.14;

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//PID gain and limit settings
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
float pid_p_gain_roll;
float pid_i_gain_roll;
float pid_d_gain_roll;
int pid_max_roll = 400;

float pid_p_gain_pitch;
float pid_i_gain_pitch;
float pid_d_gain_pitch;
int pid_max_pitch = 400;

float pid_p_gain_yaw;
float pid_i_gain_yaw;
float pid_d_gain_yaw;
int pid_max_yaw = 400;
///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

float pid_error_temp;
float pid_i_mem_roll, pid_roll_setpoint, gyro_roll_input, pid_output_roll, pid_last_roll_d_error;
float pid_i_mem_pitch, pid_pitch_setpoint, gyro_pitch_input, pid_output_pitch, pid_last_pitch_d_error;
float pid_i_mem_yaw, pid_yaw_setpoint, gyro_yaw_input, pid_output_yaw, pid_last_yaw_d_error;

int pwm_fl, pwm_fr, pwm_rl, pwm_rr;
int fl_pin = 5; //pwm_1
int fr_pin = 10; //pwm_4
int rl_pin = 6; //pwm_2
int rr_pin = 9; //pwm_3

///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

MPU6050 mpu;

int16_t ax, ay, az;
int16_t gx, gy, gz;

// MPU control/status vars
bool dmpReady = false;  // set true if DMP init was successful
uint8_t mpuIntStatus;   // holds actual interrupt status byte from MPU
uint8_t devStatus;      // return status after each device operation (0 = success, !0 = error)
uint16_t packetSize;    // expected DMP packet size (default is 42 bytes)
uint16_t fifoCount;     // count of all bytes currently in FIFO
uint8_t fifoBuffer[64]; // FIFO storage buffer

// orientation/motion vars
Quaternion q;           // [w, x, y, z]         quaternion container
VectorFloat gravity;    // [x, y, z]            gravity vector
float ypr[3];           // [yaw, pitch, roll]   yaw/pitch/roll container and gravity vector
double gyro_pitch, gyro_roll, gyro_yaw;

bool idle_state = true;
bool isHigh = true;
int timeLow = 0;

/********************************
 **** Voltage Divider Consts ****
 ********************************/
#define contBattPin A2
#define R4 10000
#define R3 18000
#define logicVolt 3.29

RF24 radio(CE, CS);
rx_values_t rxValues;
// set up controller: pass it radio, channel #, and false since it is not the controller
Controller controller(&radio, channel, false);


// ================================================================
// ===               INTERRUPT DETECTION ROUTINE                ===
// ================================================================
volatile bool mpuInterrupt = false;     // indicates whether MPU interrupt pin has gone high
void dmpDataReady()
{
  mpuInterrupt = true;
}

void setup()
{
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  pinMode(led, OUTPUT);
  pinMode(led2, OUTPUT);
  pinMode(fl_pin, OUTPUT);
  pinMode(fr_pin, OUTPUT);
  pinMode(rl_pin, OUTPUT);
  pinMode(rr_pin, OUTPUT);
  analogWrite(fl_pin, 0);
  analogWrite(fr_pin, 0);
  analogWrite(rl_pin, 0);
  analogWrite(rr_pin, 0);
  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
  if (!debug)
    delay(5000);
  if (debug) {
    Serial.begin(38400);
    while (!Serial);
  }
  controller.init();

  ///////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
#if I2CDEV_IMPLEMENTATION == I2CDEV_ARDUINO_WIRE
  Wire.begin();
  TWBR = 24; // 400kHz I2C clock (200kHz if CPU is 8MHz)
#elif I2CDEV_IMPLEMENTATION == I2CDEV_BUILTIN_FASTWIRE
  Fastwire::setup(400, true);
#endif
  if (debug)
    Serial.println(F("Initializing I2C devices..."));
  mpu.initialize();
  if (debug) {
    Serial.println(F("Testing device connections..."));
    Serial.println(mpu.testConnection() ? F("MPU6050 connection successful") : F("MPU6050 connection failed"));
  }

  // wait for ready
  //  Serial.println(F("\nSend any character to begin DMP programming and demo: "));
  //  while (Serial.available() && Serial.read()); // empty buffer
  //  while (!Serial.available());                 // wait for data
  //  while (Serial.available() && Serial.read()); // empty buffer again
  if (debug)
    Serial.println(F("Initializing DMP..."));
  devStatus = mpu.dmpInitialize();

  // supply your own gyro offsets here, scaled for min sensitivity
  //  mpu.setXAccelOffset(-3650);
  //  mpu.setYAccelOffset(1900);
  //  mpu.setZAccelOffset(800);
  //  mpu.setXGyroOffset(-40);
  //  mpu.setYGyroOffset(-210);
  //  mpu.setZGyroOffset(22);

  mpu.setXAccelOffset(-3620);
  mpu.setYAccelOffset(1890);
  mpu.setZAccelOffset(940);
  mpu.setXGyroOffset(-50);
  mpu.setYGyroOffset(-200);
  mpu.setZGyroOffset(1);

  // make sure it worked (returns 0 if so)
  if (devStatus == 0)
  {
    if (debug)
      Serial.println(F("Enabling DMP..."));
    mpu.setDMPEnabled(true);
    if (debug)
      Serial.println(F("Enabling interrupt detection (Arduino external interrupt 0)..."));
    attachInterrupt(0, dmpDataReady, RISING);
    mpuIntStatus = mpu.getIntStatus();
    if (debug)
      Serial.println(F("DMP ready! Waiting for first interrupt..."));
    dmpReady = true;

    packetSize = mpu.dmpGetFIFOPacketSize();
  }
  else if (debug)
  {
    Serial.print(F("DMP Initialization failed (code "));
    Serial.print(devStatus);
    Serial.println(F(")"));
  }

  pid_p_gain_yaw = 0.3;
  pid_i_gain_yaw = 0;
  pid_d_gain_yaw = 0;
}

void loop()
{
  //if (!dmpReady) return;
  if (!controller.isFunctioning())
  {
    if (debug)
      Serial.println("EMERGENCY!! TURN OFF ALL MOTORS AND ST  OP RUNING CODE");
    analogWrite(fl_pin, 0);
    analogWrite(fr_pin, 0);
    analogWrite(rl_pin, 0);
    analogWrite(rr_pin, 0);
    idle_state = true;
  }
  //only print values if new values have been received
  //controler.receive will return however many values were in the buffer

  if (controller.receive(&rxValues))
  {
    analogWrite(led, rxValues.throttle);
    if (debug)
    {
      Serial.print(" :\t"); Serial.print(rxValues.throttle);
      Serial.print("\t"); Serial.print(rxValues.yaw);
      Serial.print("\t"); Serial.print(rxValues.pitch);
      Serial.print("\t"); Serial.print(rxValues.roll);
      Serial.print("\t"); Serial.print(rxValues.flip);
      Serial.print("\t"); Serial.print(rxValues.highspeed);
      Serial.print("\t"); Serial.print(rxValues.P);
      Serial.print("\t"); Serial.print(rxValues.I);
      Serial.print("\t"); Serial.println(rxValues.D);
    }
  }

  //getProcessVariable();

  if (idle_state && rxValues.throttle == 0 && rxValues.pitch >= MAXPITCH)
  {
    idle_state = false;
    if (debug)
      Serial.println("Quadcopter turned on");
    rxValues.auxLED = true;
    controller.send(&rxValues);
    delay(1000);
    rxValues.auxLED = false;
    controller.send(&rxValues);
    controller.receive(&rxValues);
  }

  if (!idle_state && rxValues.throttle == 0 && rxValues.pitch <= MINPITCH)
  {
    idle_state = true;
    if (debug)
      Serial.println("Quadcopter turned off");
    delay(1000);
  }

  if (!idle_state)
  {
    getProcessVariable();
    gyro_yaw_input = gyro_yaw;
    gyro_pitch_input = gyro_pitch;
    gyro_roll_input = gyro_roll;

    pid_yaw_setpoint = 0;
    pid_pitch_setpoint = map(rxValues.pitch, 0, 255, -90, 90);
    pid_roll_setpoint = map(rxValues.roll, 0, 255, -90, 90);

    if (rxValues.flip && rxValues.yaw <= MINYAW)
    {
      pid_p_gain_yaw = rxValues.P;
      pid_i_gain_yaw = rxValues.I;
      pid_d_gain_yaw = rxValues.D;
      digitalWrite(led2, HIGH);
      delay(1000);
      digitalWrite(led2, LOW);
    }

    pid_p_gain_roll = rxValues.P;
    pid_p_gain_pitch = rxValues.P;


    pid_i_gain_roll = rxValues.I;
    pid_i_gain_pitch = rxValues.I;

    pid_d_gain_roll = rxValues.D;
    pid_d_gain_pitch = rxValues.D;


    calc_PID();


    pwm_fl = rxValues.throttle + pid_output_pitch - pid_output_roll - pid_output_yaw;
    pwm_fr = rxValues.throttle + pid_output_pitch + pid_output_roll + pid_output_yaw;
    pwm_rl = rxValues.throttle - pid_output_pitch - pid_output_roll + pid_output_yaw;
    pwm_rr = rxValues.throttle - pid_output_pitch + pid_output_roll - pid_output_yaw;


    //        pwm_fl = rxValues.throttle + pid_output_pitch - pid_output_roll;
    //        pwm_fr = rxValues.throttle + pid_output_pitch + pid_output_roll;
    //        pwm_rl = rxValues.throttle - pid_output_pitch - pid_output_roll;
    //        pwm_rr = rxValues.throttle - pid_output_pitch + pid_output_roll;

    if (pwm_fl < minPWM) pwm_fl = minPWM;
    if (pwm_fr < minPWM) pwm_fr = minPWM;
    if (pwm_rl < minPWM) pwm_rl = minPWM;
    if (pwm_rr < minPWM) pwm_rr = minPWM;

    if (pwm_fl > maxPWM) pwm_fl = maxPWM;
    if (pwm_fr > maxPWM) pwm_fr = maxPWM;
    if (pwm_rl > maxPWM) pwm_rl = maxPWM;
    if (pwm_rr > maxPWM) pwm_rr = maxPWM;

    if (debug)
    {
//            Serial.print("pwm signals \t");
//            Serial.print("front left: \t");
//            Serial.print(pwm_fl);
//            Serial.print("\t");
//            Serial.print("front right: \t");
//            Serial.print(pwm_fr);
//            Serial.print("\t");
//            Serial.print("rear left: \t");
//            Serial.print(pwm_rl);
//            Serial.print("\t");
//            Serial.print("rear right: \t");
//            Serial.println(pwm_rr);
    }

    if (!debug) {
      analogWrite(fl_pin, pwm_fl);
      analogWrite(fr_pin, pwm_fr);
      analogWrite(rl_pin, pwm_rl);
      analogWrite(rr_pin, pwm_rr);
    }
  }
  else
  {
    pid_i_mem_roll = 0;
    pid_i_mem_pitch = 0;
    pid_i_mem_yaw = 0;

    pid_last_roll_d_error = 0;
    pid_last_pitch_d_error = 0;
    pid_last_yaw_d_error = 0;

    pwm_fl = 0;
    pwm_fr = 0;
    pwm_rl = 0;
    pwm_rr = 0;

    analogWrite(fl_pin, pwm_fl);
    analogWrite(fr_pin, pwm_fr);
    analogWrite(rl_pin, pwm_rl);
    analogWrite(rr_pin, pwm_rr);
  }

  if (!debug)
    updateBattery();
  controller.send(&rxValues);
}


void getProcessVariable()
{
  dmpDataReady();
  delay(10);
  while (!mpuInterrupt && fifoCount < packetSize)
  {
  }

  mpuInterrupt = false;
  mpuIntStatus = mpu.getIntStatus();

  fifoCount = mpu.getFIFOCount();

  if ((mpuIntStatus & 0x10) || fifoCount == 1024)
  {
    mpu.resetFIFO();
    //      Serial.println(F("FIFO overflow!"));
  }
  else if (mpuIntStatus & 0x02)
  {
    while (fifoCount < packetSize) fifoCount = mpu.getFIFOCount();
    mpu.getFIFOBytes(fifoBuffer, packetSize);
    fifoCount -= packetSize;

    mpu.getRotation(&gx, &gy, &gz);
    mpu.dmpGetQuaternion(&q, fifoBuffer);
    mpu.dmpGetGravity(&gravity, &q);
    mpu.dmpGetYawPitchRoll(ypr, &q, &gravity);
    gyro_yaw = gz / 1000;
    gyro_pitch = ypr[1] * 180 / pi;
    gyro_roll = ypr[2] * 180 / pi;
    if (debug)
    {
      //            Serial.print("ypr\t");
      //            Serial.print(gyro_yaw);
      //            Serial.print("\t");
      //            Serial.print(gyro_pitch);
      //            Serial.print("\t");
      //            Serial.println(gyro_roll);
    }
  }
}

void calc_PID()
{
  pid_error_temp = gyro_roll_input - pid_roll_setpoint;
  pid_i_mem_roll += pid_i_gain_roll * pid_error_temp;
  if (pid_i_mem_roll > pid_max_roll)pid_i_mem_roll = pid_max_roll;
  else if (pid_i_mem_roll < pid_max_roll * -1)pid_i_mem_roll = pid_max_roll * -1;

  pid_output_roll = pid_p_gain_roll * pid_error_temp + pid_i_mem_roll + pid_d_gain_roll * (pid_error_temp - pid_last_roll_d_error);
  if (pid_output_roll > pid_max_roll)pid_output_roll = pid_max_roll;
  else if (pid_output_roll < pid_max_roll * -1)pid_output_roll = pid_max_roll * -1;

  pid_last_roll_d_error = pid_error_temp;

  //Pitch calculations
  pid_error_temp = gyro_pitch_input - pid_pitch_setpoint;
  pid_i_mem_pitch += pid_i_gain_pitch * pid_error_temp;
  if (pid_i_mem_pitch > pid_max_pitch)pid_i_mem_pitch = pid_max_pitch;
  else if (pid_i_mem_pitch < pid_max_pitch * -1)pid_i_mem_pitch = pid_max_pitch * -1;

  pid_output_pitch = pid_p_gain_pitch * pid_error_temp + pid_i_mem_pitch + pid_d_gain_pitch * (pid_error_temp - pid_last_pitch_d_error);
  if (pid_output_pitch > pid_max_pitch)pid_output_pitch = pid_max_pitch;
  else if (pid_output_pitch < pid_max_pitch * -1)pid_output_pitch = pid_max_pitch * -1;

  pid_last_pitch_d_error = pid_error_temp;

  //Yaw calculations
  pid_error_temp = gyro_yaw_input - pid_yaw_setpoint;
  pid_i_mem_yaw += pid_i_gain_yaw * pid_error_temp;
  if (pid_i_mem_yaw > pid_max_yaw)pid_i_mem_yaw = pid_max_yaw;
  else if (pid_i_mem_yaw < pid_max_yaw * -1)pid_i_mem_yaw = pid_max_yaw * -1;

  pid_output_yaw = pid_p_gain_yaw * pid_error_temp + pid_i_mem_yaw + pid_d_gain_yaw * (pid_error_temp - pid_last_yaw_d_error);
  if (pid_output_yaw > pid_max_yaw)pid_output_yaw = pid_max_yaw;
  else if (pid_output_yaw < pid_max_yaw * -1)pid_output_yaw = pid_max_yaw * -1;

  pid_last_yaw_d_error = pid_error_temp;
}

void updateBattery() {
  int batRead = analogRead(contBattPin);
  //calc bat voltage (mV)

  unsigned long batVolt = (batRead * logicVolt * (R3 + R4)) / (R4) * 1000 / 1023;
  //  Serial.println(batVolt);
  if (batVolt < 6000 && isHigh) {
    // tell the controller to turn on the led
    rxValues.auxLED = true;
    isHigh = false;
    timeLow = millis();
  }
  else
    isHigh = true;

  if (!isHigh && millis() - timeLow > 1000)
    idle_state = true;
}
