#include "canvas_gpio.h"

// SW_A_PIN 13
// ENCX_A_PIN 19
// ENCX_B_PIN 26

using rgb_matrix::RGBMatrix;
using rgb_matrix::Canvas;
using rgb_matrix::FrameCanvas;

volatile int encX_count = 0;

extern volatile bool interrupt_received;
extern const int number_apps;
extern volatile bool changing_app;

struct canvas_args{
  RGBMatrix *canvas;
  FrameCanvas *offscreen_canvas;
  pthread_mutex_t *canvas_mutex;
};

void* gpioThread(void *ptr){
  printf("Entered gpio thread\n");

  struct canvas_args *canvas_ptrs = (struct canvas_args*)ptr;
  RGBMatrix *canvas = canvas_ptrs->canvas;
  FrameCanvas *offscreen_canvas = canvas_ptrs->offscreen_canvas;
  pthread_mutex_t *canvas_mutex = canvas_ptrs->canvas_mutex;


  bool prev_encX_a = 0;
  bool prev_sw_pressed = 0;

  const uint64_t available_inputs = canvas->RequestInputs(0x06082000);

  fprintf(stderr, "Available GPIO-bits: ");
  for (int b = 0; b < 32; ++b) {
      if (available_inputs & (1<<b))
          fprintf(stderr, "%d ", b);
  }
  fprintf(stderr, "\n");

  while (!interrupt_received) {
    // Block and wait until any input bit changed or 100ms passed
    uint32_t inputs = canvas->AwaitInputChange(100);

    bool sw_pressed = !(inputs & 1<<SW_A_PIN); // switch is active low
    bool encX_a = (inputs & 1<<ENCX_A_PIN);
    bool encX_b = (inputs & 1<<ENCX_B_PIN);

    // Process rotary encoder
    if(encX_a != prev_encX_a) {
      if(encX_a == encX_b) {
        printf("COUNTER CLOCKWISE ROTATION - A: %d, B: %d\n", encX_a, encX_b);
        encX_count = (0 < encX_count) ? --encX_count : encX_count;
      } else {
        printf("CLOCKWISE ROTATION - A: %d, B: %d\n", encX_a, encX_b);
        encX_count = (encX_count < number_apps) ? ++encX_count : encX_count;
      }
      printf("Count = %d\n", encX_count);
    }
    prev_encX_a = encX_a;

    // Switch
    if(!prev_sw_pressed && sw_pressed) {
      printf("SWITCH PRESSED\n------------------------------CHANGING_APP = %d", changing_app);
      changing_app = !changing_app;
    }
    prev_sw_pressed = sw_pressed;

  }
}
/*
void rotateInterrupt(void) {
  int A_value = digitalRead(outputApin);
  int B_value = digitalRead(outputBpin);

  printf("A value = %d, B value = %d\n", A_value, B_value);
  // Analyze direction
  if(A_value != prev_A_value){
    if(A_value == B_value) {
      printf("Counter clockwise rotation\n");
      count--;
    } else {
      printf("Clockwise rotation\n");
      count++;
    }
    prev_A_value = A_value;
    printf("Count = %d\n", count);
  } else {printf("ROTARY ENCODER BOUNCING\n");}
}

void switchInterrupt(void){
  int A_value = digitalRead(outputApin);
  int B_value = digitalRead(outputBpin);

  printf("A value = %d, B value = %d\n", A_value, B_value);

  printf("Switch pressed\n");
}

void gpioSetup(void) {

  // GPIO Setup
  if(wiringPiSetup() == -1) {
    printf("Error setting up wiringPi\n");
    }

  printf("GPIO setup ok");

  // GPIO Pin Initialization
  pinMode(outputApin, INPUT);
  pinMode(outputBpin, INPUT);
  pinMode(switchpin, INPUT);

  // Set input as pullup
  pullUpDnControl(outputApin, PUD_UP);
  pullUpDnControl(outputBpin, PUD_UP);
  pullUpDnControl(switchpin, PUD_UP);

  wiringPiISR(outputApin, INT_EDGE_BOTH, &rotateInterrupt);
//  wiringPiISR(outputBpin, INT_EDGE_FALLING, &rotateInterrupt);
  wiringPiISR(switchpin, INT_EDGE_RISING, &switchInterrupt);
}
*/
